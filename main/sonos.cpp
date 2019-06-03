#include <Arduino.h>
#include <HTTPClient.h>
#include <AsyncUDP.h>
#include <AsyncUDP.h>
#include <string.h>
#include <expat.h>
#include <Preferences.h>
#include "sonos.h"

static const char* PLAYER_SEARCH = "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 1\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n";

static std::string soapCall(std::string operation) {
    return "<?xml version=\"1.0\"?>"  
        "<s:Envelope s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">" 
          "<s:Body>"  
            "<u:" + operation + " xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">" 
              "<InstanceID>0</InstanceID>"  
              "<Speed>1</Speed>"  
            "</u:" + operation + ">" 
          "</s:Body>"  
        "</s:Envelope>";
}

static const std::string GET_VOLUME_CALL = "<?xml version=\"1.0\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
            "<u:GetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
                "<InstanceID>0</InstanceID>"
                "<Channel>Master</Channel>"
            "</u:GetVolume>"
        "</s:Body>"
    "</s:Envelope>";

static std::string changeVolumeCall(int volume) {
    return "<?xml version=\"1.0\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
            "<u:SetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
                "<InstanceID>0</InstanceID>"
                "<Channel>Master</Channel>"
                "<DesiredVolume>" + std::string(String(volume).c_str()) + "</DesiredVolume>"
            "</u:GetVolume>"
        "</s:Body>"
    "</s:Envelope>";
}

std::string tagValue(std::string xmlData, std::string tagName) {

    typedef struct state {
        std::string captured;
        boolean capturing;
        std::string tagName;
    } ParseState;

    std::string captured;

    ParseState state = {
        captured,
        false,
        tagName
    };

    XML_StartElementHandler start = [](void *myState, const char *el, const char **attr) {
        ParseState *d = (ParseState*) myState;
        if (strncmp(el, d->tagName.c_str(), d->tagName.length()) == 0) {
            d->capturing = true;
        }
    };
    XML_EndElementHandler end = [](void *myState, const char *el) {
        ParseState *d = (ParseState*) myState;
        d->capturing = false;
    };
    XML_CharacterDataHandler charData = [](void *myState, const char *s, int len) {
        ParseState *d = (ParseState*) myState;
        if (d->capturing) {
            d->captured += std::string(s, len);
        }
    };
    XML_Parser p = XML_ParserCreate(NULL);
    if (! p) {
        Serial.println("Couldn't allocate parser");
    } else {
        XML_SetUserData(p, &state);
        XML_SetElementHandler(p, start, end);
        XML_SetCharacterDataHandler(p, charData);
        XML_Parse(p, xmlData.c_str(), xmlData.length(), true);
        XML_ParserFree(p);
    }
    return state.captured;

}

// Find the Location attribute for the player specified by uid
std::string filterDeviceLocation(std::string xmlData, std::string targetUid) {
    typedef struct state {
        std::string location;
        std::string targetUid;
    } ParseState;

    std::string location;

    ParseState state = {
        location,
        targetUid
    };

    XML_StartElementHandler start = [](void *myState, const char *el, const char **attr) {
        ParseState *state = (ParseState *) myState;
        // 15 is ZoneGroupMember length
        if (strncmp(el, "ZoneGroupMember", 15) == 0) {
            std::string loc;
            std::string uid;

            for (int i = 0;; i += 2) {
                const char *key = attr[i];
                if (key == NULL) {
                    break;
                }
                const char *value = attr[i + 1];
                if (value == NULL) {
                    break;
                }

                if (strncmp(key, "UUID", 4) == 0) {
                    uid = std::string(value);
                } else if (strncmp(key, "Location", 8) == 0) {
                    loc = std::string(value);
                }
            }
            
            if (loc.length() > 0 && (uid == state->targetUid)) {
                state->location += loc;
            }
        }
    };

    XML_Parser p = XML_ParserCreate(NULL);
    if (! p) {
        Serial.println("Couldn't allocate parser");
    } else {
        XML_SetUserData(p, &state);
        XML_SetElementHandler(p, start, NULL);
        XML_Parse(p, xmlData.c_str(), xmlData.length(), true);
        XML_ParserFree(p);
    }

    return state.location;
}


/**
 * Given any sonos' address, ask it for the zone topology to find the right sonos
 */
IPAddress zoneTopology(HTTPClient *http, std::string host, std::string targetUid) {
    auto postBody = soapCall("GetZoneGroupState");

    IPAddress ipaddr;

    if (http->begin(host.c_str(), SONOS_PORT, "/ZoneGroupTopology/Control")) {
        http->addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http->addHeader("SOAPACTION", "urn:schemas-upnp-org:service:ZoneGroupTopology:1#GetZoneGroupState");
        int httpCode = http->POST(postBody.c_str());
        if (httpCode == 200) {
            // The body here is an XML doc embedded in the body of another, so just pull out the first one, then run it through the next parser
            std::string innerXml = tagValue(std::string(http->getString().c_str()), "ZoneGroupState");
            std::string locationUrl = filterDeviceLocation(innerXml, targetUid);
            if (locationUrl.length() > 0 && locationUrl.length() > 7) {
                Serial.printf("Found location: %s\n", locationUrl.c_str());

                int pos = locationUrl.find(":", 7);
                if (pos > 0) {
                    ipaddr.fromString(locationUrl.substr(7, pos - 7).c_str());
                }
            }
            if (!ipaddr) {
                Serial.println("Couldn't find location descriptor for sonos");
            }
        } else {
            Serial.printf("Got bad status code getting zone topology %d: %s\n", httpCode, http->getString().c_str());
        }
    }
    return ipaddr;
}

IPAddress discoverSonos(std::string uid) {

    AsyncUDP udp;
    IPAddress foundAddr;
    IPAddress targetSonos;
    if (udp.listenMulticast(IPAddress(239, 255, 255, 250), 1900)) {
        Serial.println("UDP connected");
        udp.onPacket([&foundAddr](AsyncUDPPacket packet) {
            if (!foundAddr) {
                auto s = std::string((char*) packet.data());
                // All we care about here is finding a sonos, any sonos.
                if (s.find("Sonos") > 0) {
                    foundAddr = packet.remoteIP();
                }   
            } else {
                Serial.printf("Got duplicate announcement from %s\n", packet.remoteIP().toString().c_str());
            }
        });
        for (uint8_t i = 0; i < 4 && !foundAddr; i++) {
            udp.broadcast(PLAYER_SEARCH);
            delay(250);
        }
        if (foundAddr) {
            Serial.printf("Found a sonos address %s\n", foundAddr.toString().c_str());

            // Now we need to ask whatever sonos we found about the topology to find what we care about
            HTTPClient http;
            http.setConnectTimeout(HTTP_TIMEOUT);
            http.setTimeout(HTTP_TIMEOUT);

            IPAddress ourSonos = zoneTopology(&http, std::string(foundAddr.toString().c_str()), uid);
            if (ourSonos) {
                Serial.printf("FOUND OUR SONOS at %s\n", ourSonos.toString().c_str());
                targetSonos = ourSonos;
                // Save our findings in flash across boots
                Preferences prefs;
                prefs.begin("sonos");
                prefs.putString("playerAddress", targetSonos.toString());
                prefs.putString("playerUid", String(uid.c_str()));
                prefs.end();
            }
            http.end();
        } else {
            Serial.println("Nope, didn't find anything");
        }
        udp.close();
    }
    return targetSonos;
}

int sonosOperation(int (*operation)(HTTPClient *http, IPAddress target), IPAddress targetSonos) {
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(HTTP_TIMEOUT);
    http.setTimeout(HTTP_TIMEOUT);
    
    int errorCode = operation(&http, targetSonos);
    if (errorCode) {
        // I think we just care about noticing that we might have to rediscover the sonos but punt for now
        Serial.printf("Got error from sonos operation %d\n", errorCode);
    }
    http.end();
    return errorCode;
}

std::string playState(HTTPClient *http, IPAddress targetSonos) {
    auto postBody = soapCall("GetTransportInfo");

    if (http->begin(targetSonos.toString(), SONOS_PORT, "/MediaRenderer/AVTransport/Control")) {
        Serial.printf("POST: BODY %s\n", postBody.c_str());
        http->addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http->addHeader("SOAPACTION", "urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo");

        int httpCode = http->POST(postBody.c_str());
        if (httpCode == 200) {
            /* We're going to get back a soap response like this:
                <?xml version="1.0"?>
                <s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
                  <s:Body>
                    <u:GetTransportInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
                      <CurrentTransportState>PLAYING</CurrentTransportState>
                      <CurrentTransportStatus>OK</CurrentTransportStatus>
                      <CurrentSpeed>1</CurrentSpeed>
                    </u:GetTransportInfoResponse>
                  </s:Body>
                </s:Envelope>
             */
            return tagValue(std::string(http->getString().c_str()), "CurrentTransportState");
        } else {
            Serial.printf("Got http error code %d body: %s\n", httpCode, http->getString().c_str());
        }
    } else {
        Serial.printf("Couldn't connect to %s\n", targetSonos.toString().c_str());
    }
    return "";
}

int sonosPlay(HTTPClient *http, IPAddress targetSonos) {
    std::string currentState = playState(http, targetSonos);
    Serial.printf("Current play state is %s\n", currentState.c_str());

    std::string requestState = currentState == "PLAYING" ? "Pause" : "Play";
    auto postBody = soapCall(requestState);

    if (http->begin(targetSonos.toString(), SONOS_PORT, "/MediaRenderer/AVTransport/Control")) {
        Serial.printf("POST: BODY %s\n", postBody.c_str());
        http->addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http->addHeader("SOAPACTION", ("urn:schemas-upnp-org:service:AVTransport:1#" + requestState).c_str());

        int httpCode = http->POST(postBody.c_str());
        if (httpCode != 200) {
            Serial.printf("Got bad status code from sonos play operation %d\n", httpCode);
            Serial.printf("BODY: %s\n", http->getString().c_str());
            return httpCode;
        } else {
            return 0;
        }
    } else {
        Serial.println("Couldn't connect to sonos, maybe need to re-discover");
        return ENO_CANTCONNECT;
    }
}

int sonosNext(HTTPClient *http, IPAddress targetSonos) {
    auto postBody = soapCall("Next");

    if (http->begin(targetSonos.toString(), SONOS_PORT, "/MediaRenderer/AVTransport/Control")) {
        Serial.printf("POST: BODY %s\n", postBody.c_str());
        http->addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http->addHeader("SOAPACTION", "urn:schemas-upnp-org:service:AVTransport:1#Next");

        int httpCode = http->POST(postBody.c_str());
        if (httpCode != 200) {
            Serial.printf("Got bad status code from sonos next operation %d\n", httpCode);
            Serial.printf("BODY: %s\n", http->getString().c_str());
            return httpCode;
        } else {
            return 0;
        }
    } else {
        Serial.println("Couldn't connect to sonos, maybe need to re-discover");
        return ENO_CANTCONNECT;
    }

}

int getVolume(HTTPClient *http, IPAddress targetSonos) {
    if (http->begin(targetSonos.toString(), SONOS_PORT, "/MediaRenderer/RenderingControl/Control")) {
        http->addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http->addHeader("SOAPACTION", "urn:schemas-upnp-org:service:RenderingControl:1#GetVolume");

        int httpCode = http->POST(GET_VOLUME_CALL.c_str());
        if (httpCode != 200) {
            Serial.printf("Got bad status code from sonos next operation %d\n", httpCode);
            Serial.printf("BODY: %s\n", http->getString().c_str());
            return -1;
        } else {
            auto volStr = tagValue(std::string(http->getString().c_str()), "CurrentVolume");
            return String(volStr.c_str()).toInt();
        }

    } else {
        Serial.println("Couldn't connect to sonos, maybe need to re-discover");
        return -1;
    }
}

int changeVolume(HTTPClient *http, IPAddress targetSonos, int amount) {
    int currentVolume = getVolume(http, targetSonos);
    if (currentVolume < 0) {
        Serial.println("Couldn't get the current volume");
        return ENO_CANTCONNECT;
    }

    int nextVolume = currentVolume + amount;
    if (nextVolume < 0) {
        nextVolume = 0;
    } else if (nextVolume > 100) {
        nextVolume = 100;
    }

    if (http->begin(targetSonos.toString(), SONOS_PORT, "/MediaRenderer/RenderingControl/Control")) {
        auto call = changeVolumeCall(nextVolume);
        Serial.printf("POST: BODY %s\n", call.c_str());
        http->addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http->addHeader("SOAPACTION", "urn:schemas-upnp-org:service:RenderingControl:1#SetVolume");

        int httpCode = http->POST(call.c_str());
        if (httpCode != 200) {
            Serial.printf("Got bad status code from sonos next operation %d\n", httpCode);
            Serial.printf("BODY: %s\n", http->getString().c_str());
            return httpCode;
        } else {
            return 0;
        }

    } else {
        Serial.println("Couldn't connect to sonos, maybe need to re-discover");
        return -1;
    }
}

int volumeUp(HTTPClient *http, IPAddress targetSonos) {
    return changeVolume(http, targetSonos, 7);
}

int volumeDown(HTTPClient *http, IPAddress targetSonos) {
    return changeVolume(http, targetSonos, -7);
}

