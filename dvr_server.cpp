/*
 * Copyright 2026, Kris Beazley HaikuDVR@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Application.h>
#include <OS.h>
#include <SupportDefs.h>
#include <Locker.h>
#include <curl/curl.h>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <signal.h>
#include <unistd.h> 
#include <map>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include "hdhomerun.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <thread> 
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/types.h>
#include <algorithm>
#include <regex>
#include <cctype>

const uint32 MSG_ABORT_SPECIFIC_RECORDING = 'absp';

struct ActiveWorkerInfo {
    thread_id threadId;
    int32*    cancellationFlag; 
};

std::map<std::string, ActiveWorkerInfo> gRunningWorkersMap;
BLocker                                 gRunningWorkersLocker("RunningWorkersLock");


using json = nlohmann::json;

const char* kSettingsFilePath = "/boot/home/config/settings/HaikuDVR_schedules.json";

int32 gStopService = 0;
int32 gCancelRecording = 0; 

struct ScheduleItem {
    std::string startDate;
    std::string startTime;     
    time_t      epochStart;   
    int64       durationSec;   
    std::string duration;      
    std::string channel;
    std::string channelLabel; 
    std::string tunerIp;
    std::string showTitle;
    bool        processed;
};


struct RecordingConfig {
    std::string ip;
    std::string channel;
    std::string duration;
    std::string path;
    int dbIndexPosition;
    thread_id workerThread;
};

std::vector<ScheduleItem> gScheduleList;
std::string gGlobalSaveDirectory = "/boot/home";
BLocker gScheduleLocker;
bool gFrontendUpdateNotifications = true;
bool gFrontendDebugEnable = true;
bool gFrontendFullscreenEnable = true;
BString gFrontendDefaultPlayer = "MPV";



std::vector<std::string> DiscoverAllTuners() {
    std::vector<std::string> tuners;
    struct hdhomerun_discover_device_t result_list[64];
    
    hdhomerun_discover_t* ds = hdhomerun_discover_create(NULL);
    if (ds == NULL) {
        if (gFrontendDebugEnable) fprintf(stderr, "Error: Failed to create hdhomerun discovery instance\n");
        return tuners;
    }
    
    int count = hdhomerun_discover_find_devices_v2(ds, 0, 
                    HDHOMERUN_DEVICE_TYPE_TUNER, HDHOMERUN_DEVICE_ID_WILDCARD, 
                    result_list, 64);
                    
    for (int i = 0; i < count; i++) {
        uint32_t ip = result_list[i].ip_addr;
        char ip_str[32];
        
        sprintf(ip_str, "%u.%u.%u.%u", 
            (ip >> 24) & 0xFF, 
            (ip >> 16) & 0xFF, 
            (ip >> 8)  & 0xFF, 
            ip & 0xFF);
            
        tuners.push_back(std::string(ip_str));
    }
    
    hdhomerun_discover_destroy(ds);
    return tuners; 
}



std::string GetHaikuLocalIpAddress() {
    std::string detectedIp = "127.0.0.1"; // Default fallback
    struct ifaddrs* interfaces = nullptr;
    
    if (getifaddrs(&interfaces) == 0) {
        struct ifaddrs* ifa = interfaces;
        while (ifa != nullptr) {
            // Only look for active IPv4 interfaces that are not the local loopback (loop) card
            if (ifa->ifa_addr != nullptr && ifa->ifa_addr->sa_family == AF_INET) {
                std::string interfaceName(ifa->ifa_name);
                
                // Skip loopback (local loop cards)
                if (interfaceName != "loop" && interfaceName != "lo") {
                    char ipBuffer[INET_ADDRSTRLEN] = {0};
                    struct sockaddr_in* socketAddress = (struct sockaddr_in*)ifa->ifa_addr;
                    
                    inet_ntop(AF_INET, &(socketAddress->sin_addr), ipBuffer, INET_ADDRSTRLEN);
                    detectedIp = std::string(ipBuffer);
                    break; // Successfully grabbed your primary network interface IP address
                }
            }
            ifa = ifa->ifa_next;
        }
        freeifaddrs(interfaces);
    }
    return detectedIp;
}




// Unified helper utility to generate absolute epoch time from file strings
static time_t CalculateEpoch(const std::string& dateStr, const std::string& timeStr) {
    std::string fullDateTimeStr = dateStr + " " + timeStr;
    std::tm tm_struct = {};
    std::istringstream ss(fullDateTimeStr);
    ss >> std::get_time(&tm_struct, "%Y-%m-%d %H:%M");
    if (!ss.fail()) {
        // Use standard tm structure field name
        tm_struct.tm_isdst = -1; // Let the system handle DST transitions
        return std::mktime(&tm_struct);
    }
    return 0; 
}

void LoadSchedulesFromDisk() {
    std::ifstream file(kSettingsFilePath);
    if (!file.is_open()) return;

    try {
        json jIn;
        file >> jIn;
        gScheduleLocker.Lock();
        
        if (jIn.is_object()) {
            gGlobalSaveDirectory = jIn.value("save_directory", "/boot/home");
            
            // --- BACKEND PRESERVE FRONTEND SETTINGS IN RAM ---
            gFrontendUpdateNotifications = jIn.value("show_update_notifications", true);
            gFrontendDebugEnable         = jIn.value("debug_enable", true);
            gFrontendFullscreenEnable	 = jIn.value("enable_fullscreen", true);
            gFrontendDefaultPlayer       = jIn.value("default_player", "mpv").c_str();

            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate    = entry.value("date", "2026-06-23"); 
                    item.startTime    = entry.value("time", "12:00");
                    item.channel      = entry.value("channel", "5.1");
                    item.channelLabel = entry.value("channel_label", ""); // Track frontend labels safely
                    item.duration     = entry.value("duration", "1800");                    
                    item.tunerIp      = entry.value("tuner_ip", ""); 
                    item.showTitle    = entry.value("show_title", "Unknown_Show"); 
                    item.processed    = entry.value("processed", false);
                    
                    // Added calculation math metrics generation for background workers
                    item.durationSec  = std::atoll(item.duration.c_str());
                    item.epochStart   = CalculateEpoch(item.startDate, item.startTime);

                    gScheduleList.push_back(item);
                }
            }
        }
        gScheduleLocker.Unlock();
    } catch (...) {
        gScheduleLocker.Unlock();
    }
    file.close();
}



void SaveSchedulesToDisk() {
    gScheduleLocker.Lock();
    
    json jRoot = json::object();
    
    // Assign directly since gGlobalSaveDirectory is a standard std::string in the backend
    jRoot["save_directory"] = gGlobalSaveDirectory;
    
    // --- BACKEND INJECT FRONTEND SETTINGS BACK INTO THE JSON OBJECT ---
    jRoot["show_update_notifications"] = gFrontendUpdateNotifications;
    jRoot["debug_enable"]              = gFrontendDebugEnable;
    jRoot["enable_fullscreen"]         = gFrontendFullscreenEnable;
    jRoot["default_player"]            = gFrontendDefaultPlayer.String();
    
    json jSchedules = json::array();
    for (const auto& item : gScheduleList) {
        jSchedules.push_back({
            {"date", item.startDate},
            {"time", item.startTime},
            {"channel", item.channel},
            {"channel_label", item.channelLabel}, // Preserve custom labels on export
            {"duration", item.duration},
            {"processed", item.processed},
            {"tuner_ip", item.tunerIp},
            {"show_title", item.showTitle} 
        });
    }
    jRoot["schedules"] = jSchedules;
    gScheduleLocker.Unlock();
    
    std::ofstream file(kSettingsFilePath);
    if (file.is_open()) {
        file << jRoot.dump(4);
        file.close();
    }
}



size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    file->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    int32* cancelRequested = static_cast<int32*>(clientp);
    if (cancelRequested != nullptr && atomic_get(cancelRequested) == 1) {
        return 1; 
    }
    return 0; 
}

int32 BackgroundRecordingWorker(void* data) {
    RecordingConfig* config = static_cast<RecordingConfig*>(data);
    
    int32 cancelFlag = 0;
    
    gRunningWorkersLocker.Lock();
    ActiveWorkerInfo info;
    info.threadId = find_thread(nullptr); 
    info.cancellationFlag = &cancelFlag;
    gRunningWorkersMap[config->path] = info;
    gRunningWorkersLocker.Unlock();

    CURL* curl = curl_easy_init();
    if (curl) {
        std::string cleanDuration = config->duration;
        std::string url = "http://" + config->ip + ":5004/auto/v" + config->channel + "?duration=" + cleanDuration;    
        std::ofstream outputFile(config->path, std::ios::binary);
        if (outputFile.is_open()) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StorageWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outputFile);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); 
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelFlag);

            curl_easy_perform(curl);
            outputFile.close();
        } else {
           if (gFrontendDebugEnable) printf("[WORKER ERROR] Failed to open target file for writing!\n");
        }
        curl_easy_cleanup(curl);
        
        gScheduleLocker.Lock();
        if (config->dbIndexPosition >= 0 && (size_t)config->dbIndexPosition < gScheduleList.size()) {
            gScheduleList[config->dbIndexPosition].processed = 2; 
        }
        gScheduleLocker.Unlock();
        SaveSchedulesToDisk();
    }

    gRunningWorkersLocker.Lock();
    gRunningWorkersMap.erase(config->path); 
    gRunningWorkersLocker.Unlock();

    delete config;
    return 0;
}


// Standard SSDP Configuration Values
#define SSDP_MULTICAST_IP "239.255.255.250"
#define SSDP_PORT 1900

// Thread function signature for background execution matching Haiku standards
static int32 dlna_discovery_worker_thread(void* data);

class DlnasDiscoveryServer {
public:
    thread_id serverThread;
    int       socketFd;
    int       httpPort;
    std::string localIp;

    DlnasDiscoveryServer() : serverThread(-1), socketFd(-1), httpPort(8080), localIp("0.0.0.0") {}

    ~DlnasDiscoveryServer() {
        Stop();
    }

    void Start(const std::string& ipAddress, int port) {
        localIp = ipAddress;
        httpPort = port;
        
        // Use Haiku's native multi-threading architecture
        serverThread = spawn_thread(dlna_discovery_worker_thread, "DLNA_SSDP_Engine", B_NORMAL_PRIORITY, this);
        if (serverThread >= 0) {
            resume_thread(serverThread);
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND] DLNA SSDP Discovery service thread spawned successfully.\n");
        } else {
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND ERROR] Failed to spawn DLNA SSDP thread!\n");
        }
    }

    void Stop() {
        if (socketFd >= 0) {
            // Closing the socket breaks recvfrom() and unlocks the worker thread instantly
            close(socketFd);
            socketFd = -1;
        }
        if (serverThread >= 0) {
            status_t exitValue;
            wait_for_thread(serverThread, &exitValue);
            serverThread = -1;
        }
    }
};



// Helper function to dynamically pull the live system IP
static std::string GetLiveSystemIP() {
    std::string detectedIp = "0.0.0.0";
    struct ifaddrs* interfaces = nullptr;
    
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name || !ifa->ifa_addr || !(ifa->ifa_flags & IFF_UP)) continue;
            
            if (ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
                struct sockaddr_in* ethAddr = (struct sockaddr_in*)ifa->ifa_addr;
                char ipBuffer[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &(ethAddr->sin_addr), ipBuffer, INET_ADDRSTRLEN)) {
                    detectedIp = ipBuffer;
                    if (detectedIp != "0.0.0.0" && !detectedIp.empty()) break; 
                }
            }
        }
        freeifaddrs(interfaces);
    }
    return detectedIp;
}

static int32 dlna_discovery_worker_thread(void* data) {
    DlnasDiscoveryServer* server = static_cast<DlnasDiscoveryServer*>(data);
    if (!server) return B_BAD_VALUE;

    char buffer[4096];
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    if (gFrontendDebugEnable) std::printf("[DVR BACKEND] SSDP Engine active with automatic healing pulse loop.\n");

    // Monitor application state flag continuously
    while (atomic_get(&gStopService) == 0) {
        
        // 1. DYNAMIC SOCKET MONITOR: If socket is uninitialized or dropped, try to build it
        if (server->socketFd < 0) {
            std::string liveIp = GetLiveSystemIP();
            
            // If the system hasn't obtained an IP address yet, pulse wait and retry
            if (liveIp == "0.0.0.0" || liveIp.empty()) {
                // Sleep for 5 seconds checking for shutdown requests every 500ms
                for (int i = 0; i < 10 && atomic_get(&gStopService) == 0; i++) {
                    usleep(500000);
                }
                continue; // Re-evaluate loop
            }

            // Lock in the live IP
            server->localIp = liveIp;
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND] Network link validated at %s. Binding SSDP engine...\n", server->localIp.c_str());

            // Try creating the UDP socket
            server->socketFd = socket(AF_INET, SOCK_DGRAM, 0);
            if (server->socketFd < 0) {
                usleep(5000000); // Wait 5 seconds on fatal socket allocation error
                continue;
            }

            // Set socket options for immediate re-use
            int reuse = 1;
            if (setsockopt(server->socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
                close(server->socketFd);
                server->socketFd = -1;
                continue;
            }

            // Bind explicitly to port 1900
            struct sockaddr_in localAddr{};
            localAddr.sin_family = AF_INET;
            localAddr.sin_port = htons(SSDP_PORT);
            localAddr.sin_addr.s_addr = INADDR_ANY; 

            if (bind(server->socketFd, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
                if (gFrontendDebugEnable) std::printf("[DLNA ERROR] SSDP Bind failed on port 1900. Retrying in 5s...\n");
                close(server->socketFd);
                server->socketFd = -1;
                // Sleep for 5 seconds checking for shutdown requests
                for (int i = 0; i < 10 && atomic_get(&gStopService) == 0; i++) usleep(500000);
                continue;
            }

            // JOIN the Multicast Group locked to our newly confirmed live system IP
            struct ip_mreq group{};
            group.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST_IP);
            group.imr_interface.s_addr = inet_addr(server->localIp.c_str()); 
            
            if (setsockopt(server->socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
                if (gFrontendDebugEnable) std::printf("[DLNA ERROR] Joining multicast group failed on interface %s\n", server->localIp.c_str());
                close(server->socketFd);
                server->socketFd = -1;
                for (int i = 0; i < 10 && atomic_get(&gStopService) == 0; i++) usleep(500000);
                continue;
            }

            // Set a non-blocking timeout of 5 seconds on the socket read operation.
            // This prevents the thread from blocking infinitely on recvfrom and allows the pulse to check its health.
            struct timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            setsockopt(server->socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            if (gFrontendDebugEnable) std::printf("[DVR BACKEND] SSDP Engine successfully online and listening.\n");
        }

        // 2. NETWORK PACKET RECEIVE
        std::memset(buffer, 0, sizeof(buffer));
        addrLen = sizeof(clientAddr);
        ssize_t bytesRead = recvfrom(server->socketFd, buffer, sizeof(buffer) - 1, 0, 
                                     (struct sockaddr*)&clientAddr, &addrLen);
        
        if (bytesRead < 0) {
            // Check if it was just a 5-second socket timeout pulse
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Heartbeat Pulse Check: Confirm that our bound interface IP hasn't shifted underneath us
                std::string currentCheckIP = GetLiveSystemIP();
                if (currentCheckIP != server->localIp) {
                    if (gFrontendDebugEnable) std::printf("[DVR BACKEND WARNING] System IP change or interface reset detected! Healing socket...\n");
                    close(server->socketFd);
                    server->socketFd = -1; // Forces the top of the loop to rebuild everything next cycle
                }
                continue;
            }
            
            // Socket was explicitly closed by Stop() or dropped catastrophically
            if (atomic_get(&gStopService) != 0) break;
            
            close(server->socketFd);
            server->socketFd = -1;
            continue;
        }

        // 3. PROCESS SSDP MATCHES
        std::string packetStr(buffer);
        if (packetStr.find("M-SEARCH") != std::string::npos) {
            if (packetStr.find("ssdp:all") != std::string::npos || 
                packetStr.find("urn:schemas-upnp-org:device:MediaServer:1") != std::string::npos) {
                
                char response[1024];
                std::snprintf(response, sizeof(response),
                    "HTTP/1.1 200 OK\r\n"
                    "CACHE-CONTROL: max-age=1800\r\n"
                    "LOCATION: http://%s:%d/description.xml\r\n"
                    "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
                    "USN: uuid:fe80::haiku:dvr::urn:schemas-upnp-org:device:MediaServer:1\r\n"
                    "EXT:\r\n"
                    "SERVER: HaikuOS HaikuDVR-MediaServer/1.0\r\n"
                    "\r\n", 
                    server->localIp.c_str(), server->httpPort
                );

                sendto(server->socketFd, response, std::strlen(response), 0, 
                       (struct sockaddr*)&clientAddr, sizeof(clientAddr));
            }
        }
    }

    // Clean final thread drop
    if (server->socketFd >= 0) {
        close(server->socketFd);
        server->socketFd = -1;
    }
    return B_OK;
}


class DlnasHttpStreamingServer {
public:
    thread_id serverThread;
    int       listenFd;
    int       port;
    std::string rootDir;
	std::string localIp; 
	
    DlnasHttpStreamingServer() : serverThread(-1), listenFd(-1), port(8080) {}



// Helper utility to grab the actual system IP address
static std::string GetLiveHttpSystemIP() {
    std::string detectedIp = "0.0.0.0";
    struct ifaddrs* interfaces = nullptr;
    
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name || !ifa->ifa_addr || !(ifa->ifa_flags & IFF_UP)) continue;
            
            if (ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
                struct sockaddr_in* ethAddr = (struct sockaddr_in*)ifa->ifa_addr;
                char ipBuffer[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &(ethAddr->sin_addr), ipBuffer, INET_ADDRSTRLEN)) {
                    detectedIp = ipBuffer;
                    if (detectedIp != "0.0.0.0" && !detectedIp.empty()) break; 
                }
            }
        }
        freeifaddrs(interfaces);
    }
    return detectedIp;
}

static int32 HttpWorkerLoop(void* data) {
    DlnasHttpStreamingServer* self = static_cast<DlnasHttpStreamingServer*>(data);
    if (!self) return B_BAD_VALUE;
    if (gFrontendDebugEnable) {
    	std::printf("[HTTP DEBUG] Creating master TCP stream socket...\n");
    	std::fflush(stdout);
    }
    
    self->listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (self->listenFd < 0) {
    		if (gFrontendDebugEnable) {
        		std::printf("[HTTP DEBUG ERROR] Failed to create socket: %s\n", strerror(errno));
        		std::fflush(stdout);
    		}
        return B_ERROR;
    }

    int reuse = 1;
    setsockopt(self->listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in srvAddr{};
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons(self->port);
    srvAddr.sin_addr.s_addr = INADDR_ANY; // Listening on all interfaces is correct
	if (gFrontendDebugEnable) {
    	std::printf("[HTTP DEBUG] Attempting to bind TCP server to port %d...\n", self->port);
    	std::fflush(stdout);
	}
    if (bind(self->listenFd, (struct sockaddr*)&srvAddr, sizeof(srvAddr)) < 0) {
    	if (gFrontendDebugEnable) {
        	std::printf("[HTTP DEBUG ERROR] Bind to port %d failed: %s\n", self->port, strerror(errno));
        	std::fflush(stdout);
    	}
        close(self->listenFd);
        self->listenFd = -1;
        return B_ERROR;
    }
    
    listen(self->listenFd, 10);
    if (gFrontendDebugEnable) {
    	std::printf("[HTTP DEBUG] Server socket bound! Actively listening for TV requests...\n");
    	std::fflush(stdout);
    }

    // --- LIVE IP HEALING AND ACQUISITION ---
    // If we booted with a stale or blank IP address configuration, poll until one arrives
    while ((self->localIp == "0.0.0.0" || self->localIp.empty()) && atomic_get(&gStopService) == 0) {
        std::string checkedIp = GetLiveHttpSystemIP();
        if (checkedIp != "0.0.0.0" && !checkedIp.empty()) {
            self->localIp = checkedIp;
            if (gFrontendDebugEnable) {
            	std::printf("[HTTP DEBUG] Dynamic IP discovered and assigned: %s\n", self->localIp.c_str());
            	std::fflush(stdout);
            }
            break;
        }
        
        // Check for application shutdown requests while waiting for the network links
        for (int i = 0; i < 4 && atomic_get(&gStopService) == 0; i++) {
            usleep(500000); // 2-second polling increments split up safely
        }
    }
    // --- END OF IP HEALING ---

    while (atomic_get(&gStopService) == 0) {
        struct sockaddr_in cliAddr{};
        socklen_t cliLen = sizeof(cliAddr);
        
        // This is a blocking call waiting for a browser or TV to connect
        int clientFd = accept(self->listenFd, (struct sockaddr*)&cliAddr, &cliLen);
        if (clientFd < 0) {
            if (atomic_get(&gStopService) != 0) break;
            if (gFrontendDebugEnable) {
            	std::printf("[HTTP DEBUG WARNING] Accept connection failed!\n");
            	std::fflush(stdout);
            }
            continue;
        }

        // Heartbeat verification: If the system IP shifted, dynamically re-update it on the fly
        std::string validationIp = GetLiveHttpSystemIP();
        if (validationIp != "0.0.0.0" && validationIp != self->localIp) {
            self->localIp = validationIp;
            if (gFrontendDebugEnable) {
            	std::printf("[HTTP DEBUG] Network interface modified. Synchronizing server tracking to: %s\n", self->localIp.c_str());
            	std::fflush(stdout);
            }
        }

        char clientIpStr[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &(cliAddr.sin_addr), clientIpStr, INET_ADDRSTRLEN);
        if (gFrontendDebugEnable) {
        	std::printf("\n--------------------------------------------------\n");
        	std::printf("[HTTP DEBUG] TCP Handshake SUCCESS! Connected client IP: %s\n", clientIpStr);
        	std::printf("[HTTP DEBUG] Spawning background handler thread...\n");
        	std::fflush(stdout);
        }

        std::thread handler([self, clientFd, clientIpStr]() {
        	if (gFrontendDebugEnable) {
            	std::printf("[HTTP THREAD] Thread started for client: %s\n", clientIpStr);
            	std::fflush(stdout);
        	}

            std::string req;
            char chunk[2048];
            ssize_t bytesRecv = 0;
            int totalBytes = 0;

                while (true) {
                    std::memset(chunk, 0, sizeof(chunk));
                    bytesRecv = recv(clientFd, chunk, sizeof(chunk) - 1, 0);
                    
                    if (bytesRecv <= 0) break; // Client disconnected or finished

                    req.append(chunk, bytesRecv);
                    totalBytes += bytesRecv;

                    // Stop reading if we have captured the full standard UPnP SOAP envelope
                    if (req.find("</s:Envelope>") != std::string::npos || 
                        req.find("</SOAP-ENV:Envelope>") != std::string::npos) {
                        break;
                    }
                    
                    // If it's a GET request (like description.xml), it has no body, so stop after headers
                    if (req.find("GET ") == 0 && req.find("\r\n\r\n") != std::string::npos) {
                        break;
                    }
                    
                    // Safety break to avoid reading forever on oversized malformed data
                    if (totalBytes > 16384) break;
                }
                
                if (gFrontendDebugEnable) {
                    std::printf("[HTTP THREAD] recv() loop finished! Total bytes read: %d\n", totalBytes);
                    std::fflush(stdout);
                }

                if (totalBytes <= 0) {
                    if (gFrontendDebugEnable) {
                        std::printf("[HTTP THREAD WARNING] Client dropped connection or sent empty packet.\n");
                        std::fflush(stdout);
                    }
                    close(clientFd);
                    return;
                }


                
                // Print out the raw first line of the HTTP request to inspect headers
                size_t firstNewLine = req.find("\r\n");
                std::string firstLine = (firstNewLine != std::string::npos) ? req.substr(0, firstNewLine) : req;
                
                if (gFrontendDebugEnable) {
                    std::printf("[HTTP THREAD] Request Line: \"%s\"\n", firstLine.c_str());
                    std::fflush(stdout);
                }

                if (req.find("GET /description.xml") != std::string::npos) {
                    if (gFrontendDebugEnable) {
                        std::printf("[HTTP THREAD] Route matched: GET description.xml\n");
                        std::fflush(stdout);
                    }
                    self->ServeDescription(clientFd);                    
                } else if (req.find("GET /ContentDirectory/scpd.xml") != std::string::npos) {
                    if (gFrontendDebugEnable) {
                        std::printf("[HTTP THREAD] Route matched: GET scpd.xml\n");
                        std::fflush(stdout);
                    }
                    self->ServeScpd(clientFd);   
                                          
                } else if ((req.find("POST ") == 0 || req.find("\nPOST ") != std::string::npos) && 
                           (req.find("/ContentDirectory/control") != std::string::npos || 
                            req.find("/ctl/ContentDir") != std::string::npos)) {
                    
                    if (gFrontendDebugEnable) {
                        std::printf("[HTTP THREAD] Route matched safely: POST ContentDirectory\n");
                        std::fflush(stdout);
                    }
                    // Pass req down to the browser block
                    self->ServeContentDirectory(clientFd, req);

                } else if (req.find("GET /video/") != std::string::npos) {
                    if (gFrontendDebugEnable) {
                        std::printf("[HTTP THREAD] Route matched: GET Video Streaming\n");
                        std::fflush(stdout);
                    }
                    self->StreamVideoFile(clientFd, req);
                } else {
                    if (gFrontendDebugEnable) {
                        std::printf("[HTTP THREAD WARNING] Route fallback: Sending 404 Not Found\n");
                        std::fflush(stdout);
                    }
                    const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    send(clientFd, notFound, std::strlen(notFound), 0);
                }
                
                if (gFrontendDebugEnable) {
                    std::printf("[HTTP THREAD] Closing client socket file descriptor.\n");
                    std::printf("--------------------------------------------------\n");
                    std::fflush(stdout);
                }
                close(clientFd);
            });
            handler.detach();
        }

        
        if (gFrontendDebugEnable) {
            std::printf("[HTTP DEBUG] Shutting down HTTP loop container.\n");
            std::fflush(stdout);
        }
        close(self->listenFd);
        return B_OK;
    }



    void Start(int serverPort, const std::string& directory, const std::string& ipAddress) {
        port = serverPort;
        rootDir = directory;
        localIp = ipAddress;
        serverThread = spawn_thread(HttpWorkerLoop, "DLNA_HTTP_Engine", B_NORMAL_PRIORITY, this);
        if (serverThread >= 0) resume_thread(serverThread);
    }

private:
private:
    void ServeScpd(int clientFd) {
        // Industry-standard UPnP ContentDirectory definition block tracking the "Browse" action
        std::string xml = 
            "<?xml version=\"1.0\"?>\r\n"
            "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
            "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
            "  <actionList>\r\n"
            "    <action>\r\n"
            "      <name>Browse</name>\r\n"
            "      <argumentList>\r\n"
            "        <argument>\r\n"
            "          <name>ObjectID</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>BrowseFlag</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>Filter</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>StartingIndex</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>RequestedCount</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>SortCriteria</name>\r\n"
            "          <direction>in</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>Result</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>NumberReturned</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>TotalMatches</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "        <argument>\r\n"
            "          <name>UpdateID</name>\r\n"
            "          <direction>out</direction>\r\n"
            "          <relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable>\r\n"
            "        </argument>\r\n"
            "      </argumentList>\r\n"
            "    </action>\r\n"
            "  </actionList>\r\n"
            "  <serviceStateTable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_ObjectID</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_BrowseFlag</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Filter</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Index</name>\r\n"
            "      <dataType>ui4</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Count</name>\r\n"
            "      <dataType>ui4</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_SortCriteria</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_Result</name>\r\n"
            "      <dataType>string</dataType>\r\n"
            "    </stateVariable>\r\n"
            "    <stateVariable sendEvents=\"no\">\r\n"
            "      <name>A_ARG_TYPE_UpdateID</name>\r\n"
            "      <dataType>ui4</dataType>\r\n"
            "    </stateVariable>\r\n"
            "  </serviceStateTable>\r\n"
            "</scpd>\r\n";

        std::string resp = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/xml; charset=\"utf-8\"\r\n"
            "Content-Length: " + std::to_string(xml.length()) + "\r\n"
            "Connection: close\r\n\r\n" + xml;

        send(clientFd, resp.c_str(), resp.length(), 0);
    }

    void ServeDescription(int clientFd) {
        std::string xml = 
            "<?xml version=\"1.0\"?>\r\n"
            "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
            "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
            "  <device>\r\n"
            "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\r\n"
            "    <friendlyName>Haiku OS DVR Server</friendlyName>\r\n"
            "    <manufacturer>Haiku Community</manufacturer>\r\n"
            "    <modelName>HaikuDVR</modelName>\r\n"
            "    <UDN>uuid:fe80::haiku:dvr</UDN>\r\n"
            
            // Provide explicit routing tables so the TV knows where to send POST browse packets
            "    <serviceList>\r\n"
            "      <service>\r\n"
            "        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>\r\n"
            "        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>\r\n"
            "        <controlURL>/ContentDirectory/control</controlURL>\r\n"
            "        <eventSubURL>/ContentDirectory/event</eventSubURL>\r\n"
            "        <SCPDURL>/ContentDirectory/scpd.xml</SCPDURL>\r\n"
            "      </service>\r\n"
            "    </serviceList>\r\n"
            
            "  </device>\r\n"
            "</root>\r\n";

        std::string resp = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/xml; charset=\"utf-8\"\r\n"
            "Content-Length: " + std::to_string(xml.length()) + "\r\n"
            "Connection: close\r\n\r\n" + xml;

        send(clientFd, resp.c_str(), resp.length(), 0);
    }


    // Helper function to escape reserved XML control characters
    std::string EscapeXml(const std::string& data) {
        std::string buffer;
        buffer.reserve(data.size());
        for (size_t pos = 0; pos != data.size(); ++pos) {
            switch (data[pos]) {
                case '&':  buffer.append("&amp;");       break;
                case '\"': buffer.append("&quot;");      break;
                case '\'': buffer.append("&apos;");      break;
                case '<':  buffer.append("&lt;");        break;
                case '>':  buffer.append("&gt;");        break;
                default:   buffer.append(&data[pos], 1); break;
            }
        }
        return buffer;
    }
    
    




	std::string CleanFilename(const std::string& rawName) {
	    std::string clean = rawName;
	    
	    // Match prefix: "DVR_Record_Ch_" followed by channel, date, and time patterns
	    // e.g., "DVR_Record_Ch_2.1_2026-06-28_19-30_"
	    std::regex prefixRegex("^DVR_Record_Ch_\\d+\\.\\d+_\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}_");
	    clean = std::regex_replace(clean, prefixRegex, "");
	    
	    // Match suffix: recording duration, padded tag, and file extension
	    // e.g., "_12480s_Padded.ts" or "_7200s_Padded.ts"
	    std::regex suffixRegex("_\\d+s_Padded\\.(ts|mpg)$");
	    clean = std::regex_replace(clean, suffixRegex, "");
	    
	    // Replace remaining underscores with spaces for human readability
	    std::replace(clean.begin(), clean.end(), '_', ' ');
	    
	    return clean;
	}
	
	
	std::string UrlEncodeFilename(const std::string& value) {
	    std::ostringstream escaped;
	    escaped << std::hex << std::uppercase;
	    for (char c : value) {
	        // Safe unreserved URL characters
	        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
	            escaped << c;
	        } else {
	            // Converts characters like spaces to %20 and single quotes to %27
	            escaped << '%' << std::setw(2) << std::setfill('0') << (static_cast<int>(c) & 0xFF);
	        }
	    }
	    return escaped.str();
	}



    void ServeContentDirectory(int clientFd, const std::string& requestStr) {
        if (gFrontendDebugEnable) {
            std::printf("\n==================================================\n");
            std::printf("[DLNA DEBUG] Media Client requested folder data parsing!\n");
        }
        
        if (rootDir.empty()) {
            rootDir = "/boot/home/Recordings";
        }
        std::fflush(stdout);

        std::string targetObjectId = "0";
        if (requestStr.find("<ObjectID>3</ObjectID>") != std::string::npos || 
            requestStr.find("ObjectID=\"3\"") != std::string::npos ||
            requestStr.find(">3<") != std::string::npos) {
            targetObjectId = "3";
        }

        if (gFrontendDebugEnable) {
            std::printf("[DLNA DEBUG] TV Client is browsing ObjectID: %s\n", targetObjectId == "0" ? "0 (Root Tree)" : "3 (Videos Folder)");
            std::fflush(stdout);
        }

        std::string didlBody = 
            "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
            "xmlns:dc=\"http://purl.org\" "
            "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
            "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
        
        int itemsReturned = 0;

        if (targetObjectId == "0") {
            int actualVideoCount = 0;
            DIR* countDir = opendir(rootDir.c_str());
            if (countDir) {
                struct dirent* entry;
                while ((entry = readdir(countDir)) != nullptr) {
                    std::string name(entry->d_name);
                    if (name.find(".ts") != std::string::npos || name.find(".mpg") != std::string::npos) {
                        actualVideoCount++;
                    }
                }
                closedir(countDir);
            }

            didlBody += "<container id=\"3\" parentID=\"0\" childCount=\"" + std::to_string(actualVideoCount) + "\" restricted=\"1\">"
                        "<dc:title>Recordings</dc:title>"
                        "<upnp:class>object.container.storageFolder</upnp:class>"
                        "</container>";
            itemsReturned++;
            
        } else {
            int trackId = 100; 
            DIR* dir = opendir(rootDir.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name(entry->d_name);
                    
                    if (name.find(".ts") != std::string::npos || name.find(".mpg") != std::string::npos) {
                        
                        // Percent-encode ONLY the filename part of the URL string
                        std::string encodedName = UrlEncodeFilename(name);
                        std::string streamUrl = "http://" + localIp + ":" + std::to_string(port) + "/video/" + encodedName;
                        
                        if (gFrontendDebugEnable) {
                            std::printf("  -> MATCH: Listing file inside folder 3: %s\n", name.c_str());
                            std::printf("     URLEncoded Stream Link: %s\n", streamUrl.c_str());
                            std::fflush(stdout);
                        }
                        
                        // Parse the raw filename to present a clean visual title to the DLNA client
                        std::string prettyName = CleanFilename(name);
                        
                        // Dynamic profile structure declaring exact North American MPEG-TS feeds
                        std::string dlnaProfile = "http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_NA_ISO;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";

                        didlBody += "<item id=\"" + std::to_string(trackId++) + "\" parentID=\"3\" restricted=\"1\">"
                                    "<dc:title>" + prettyName + "</dc:title>" 
                                    "<upnp:class>object.item.videoItem.movie</upnp:class>"
                                    "<res protocolInfo=\"" + dlnaProfile + "\">" + streamUrl + "</res>" 
                                    "</item>";
                        itemsReturned++;
                    }
                }
                closedir(dir);
            }
        }


        didlBody += "</DIDL-Lite>";

        std::string escapedDidl = EscapeXml(didlBody);

        std::string soap = 
            "<s:Envelope xmlns:s=\"http://xmlsoap.org\" "
            "s:encodingStyle=\"http://xmlsoap.org\">"
            "<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
            "<Result>" + escapedDidl + "</Result>"
            "<NumberReturned>" + std::to_string(itemsReturned) + "</NumberReturned>"
            "<TotalMatches>" + std::to_string(itemsReturned) + "</TotalMatches>"
            "<UpdateID>1</UpdateID>"
            "</u:BrowseResponse></s:Body></s:Envelope>";

        std::string resp = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/xml; charset=\"utf-8\"\r\n"
            "Content-Length: " + std::to_string(soap.length()) + "\r\n"
            "Connection: close\r\n\r\n" + soap;

        if (gFrontendDebugEnable) {
            std::printf("[DLNA DEBUG] Transmitting hierarchical asset block size: %zu bytes\n", resp.length());
            std::printf("==================================================\n\n");
            std::fflush(stdout);
        }

        send(clientFd, resp.c_str(), resp.length(), 0);
    }




	// Helper utility to convert %27 back to "'" and %20 back to spaces
	std::string UrlDecodeFilename(const std::string& value) {
	    std::string result;
	    result.reserve(value.length());
	    
	    for (size_t i = 0; i < value.length(); ++i) {
	        if (value[i] == '%' && i + 2 < value.length()) {
	            std::string hexStr = value.substr(i + 1, 2);
	            char chr = static_cast<char>(std::strtol(hexStr.c_str(), nullptr, 16));
	            result += chr;
	            i += 2; 
	        } else if (value[i] == '+') {
	            result += ' '; 
	        } else {
	            result += value[i];
	        }
	    }
	    return result;
	}
	
	void StreamVideoFile(int clientFd, const std::string& req) {
	    size_t pos = req.find("/video/");
	    size_t endPos = req.find(" ", pos);
	    if (pos == std::string::npos || endPos == std::string::npos) return;
	    
	    std::string rawFilename = req.substr(pos + 7, endPos - (pos + 7));
	    
	    // Decode the URL characters before referencing the storage path
	    std::string filename = UrlDecodeFilename(rawFilename);
	    std::string fullPath = rootDir + "/" + filename;
	
	    if (gFrontendDebugEnable) {
	        std::printf("[DLNA STREAM] Incoming URI segment: %s\n", rawFilename.c_str());
	        std::printf("[DLNA STREAM] Reconstructed disk path: %s\n", fullPath.c_str());
	        std::fflush(stdout);
	    }
	
	    // Open the file with ate (at the end) to read total file size
	    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
	    if (!file.is_open()) {
	        if (gFrontendDebugEnable) {
	            std::printf("[DLNA ERROR] File open failed for path: %s\n", fullPath.c_str());
	            std::fflush(stdout);
	        }
	        const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
	        send(clientFd, notFound, std::strlen(notFound), 0);
	        return;
	    }
	
	    std::streamsize fileSize = file.tellg();
	    std::streamsize startByte = 0;
	    std::streamsize endByte = fileSize - 1;
	    bool isPartial = false;
	
	    // PARSE HTTP RANGE HEADERS: Look for timeline scrubbing requests
	    size_t rangePos = req.find("Range: bytes=");
	    if (rangePos != std::string::npos) {
	        size_t startIdx = rangePos + 13;
	        size_t dashIdx = req.find("-", startIdx);
	        if (dashIdx != std::string::npos) {
	            std::string startStr = req.substr(startIdx, dashIdx - startIdx);
	            if (!startStr.empty()) {
	                startByte = std::atoll(startStr.c_str());
	                isPartial = true;
	            }
	            
	            size_t nlnIdx = req.find("\r\n", dashIdx);
	            if (nlnIdx != std::string::npos) {
	                std::string endStr = req.substr(dashIdx + 1, nlnIdx - (dashIdx + 1));
	                if (!endStr.empty()) {
	                    endByte = std::atoll(endStr.c_str());
	                }
	            }
	        }
	    }

        // Seek the file pointer straight to the requested byte position
        file.seekg(startByte, std::ios::beg);
        std::streamsize contentLength = (endByte - startByte) + 1;

        // Changed from char header to a 1KB buffer array
        char header[1024] = {0}; 
        if (isPartial) {
        	if (gFrontendDebugEnable) {
            	std::printf("[SEEK SYSTEM] Client scrubbing to offset position: %lld / %lld bytes\n", (long long)startByte, (long long)fileSize);
            	std::fflush(stdout);
        	}
            
            std::snprintf(header, sizeof(header),
                "HTTP/1.1 206 Partial Content\r\n"
                "Content-Type: video/mp2t\r\n"
                "Content-Range: bytes %lld-%lld/%lld\r\n"
                "Content-Length: %lld\r\n"
                "Connection: close\r\n\r\n",
                (long long)startByte, (long long)endByte, (long long)fileSize, (long long)contentLength
            );
        } else {
        	if (gFrontendDebugEnable) {
            	std::printf("[SEEK SYSTEM] Client streaming video file from absolute beginning.\n");
            	std::fflush(stdout);
        	}
            std::snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: video/mp2t\r\n"
                "Content-Length: %lld\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: close\r\n\r\n",
                (long long)fileSize
            );
        }

        // Transmit streaming response headers
        send(clientFd, header, std::strlen(header), 0);

        // Pipe chunks smoothly from the file offset straight down into the network card socket
        char chunk[64 * 1024]; 
        std::streamsize bytesRemaining = contentLength;

        // Ensured atomic loop check and proper tracking of partial network writes
        while (bytesRemaining > 0 && file.good() && atomic_get(&gStopService) == 0) {
            std::streamsize toRead = sizeof(chunk);
            if (toRead > bytesRemaining) toRead = bytesRemaining;

            file.read(chunk, toRead);
            std::streamsize bytesRead = file.gcount();
            if (bytesRead <= 0) break;

            // Nested loop to ensure the ENTIRE read block is fully pushed to the network socket
            std::streamsize bytesSentTotal = 0;
            while (bytesSentTotal < bytesRead && atomic_get(&gStopService) == 0) {
                ssize_t sent = send(clientFd, chunk + bytesSentTotal, bytesRead - bytesSentTotal, 0);
                if (sent < 0) {
                    // Client disconnected, closed the window, or skipped away
                    bytesRemaining = 0; 
                    break;
                }
                bytesSentTotal += sent;
                bytesRemaining -= sent;
            }
            
            if (bytesRemaining <= 0) break;
        }
        file.close();
        
        if (gFrontendDebugEnable) {
            std::printf("[DLNA STREAM] Finished streaming media block to Client. Socket cleanly closing.\n");
            std::fflush(stdout);
        }
    }
};




int32 ServiceSchedulerLoop(void* data) {    
    while (atomic_get(&gStopService) == 0) {
        snooze(5000000); 
        LoadSchedulesFromDisk(); 

        std::time_t now = std::time(nullptr);
        std::time_t nextMinTime = now + 60; 
        
        std::tm* localTime = std::localtime(&now);
        std::tm* localNextTime = std::localtime(&nextMinTime);
        
        char dateBuf[32];
        char timeBuf[32];
        char nextTimeBuf[32];
        char nextDateBuf[32];
        
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localTime);
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M", localTime);
        std::strftime(nextDateBuf, sizeof(nextDateBuf), "%Y-%m-%d", localNextTime);
        std::strftime(nextTimeBuf, sizeof(nextTimeBuf), "%H:%M", localNextTime);
        
        std::string curDate(dateBuf);
        std::string curTime(timeBuf);
        std::string nextDate(nextDateBuf);
        std::string nextTime(nextTimeBuf);
        bool databaseUpdated = false;

        gScheduleLocker.Lock();
        
        for (size_t i = 0; i < gScheduleList.size(); i++) {
            if (gScheduleList[i].processed) continue;

            std::string normalizedTargetTime = gScheduleList[i].startTime;
            if (normalizedTargetTime.length() == 4 && normalizedTargetTime.find(':') == 1) {
                normalizedTargetTime = "0" + normalizedTargetTime;
            }

            bool shouldFire = false;

            if (gScheduleList[i].startDate < curDate) {
                shouldFire = true; 
            } else if (gScheduleList[i].startDate == curDate && normalizedTargetTime <= curTime) {
                shouldFire = true;
            }
            
            else if (gScheduleList[i].startDate == nextDate && normalizedTargetTime == nextTime) {
                shouldFire = true;
            }

            if (gScheduleList[i].processed == 0 && shouldFire) {    
                databaseUpdated = true;
            
                RecordingConfig* rec = new RecordingConfig();
                rec->channel = gScheduleList[i].channel;
                
                int totalDurationSeconds = std::atoi(gScheduleList[i].duration.c_str()) + 60;
                rec->duration = std::to_string(totalDurationSeconds); 
            
                bool tunerFound = false;
                if (!gScheduleList[i].tunerIp.empty()) {
                    rec->ip = gScheduleList[i].tunerIp;
                    tunerFound = true;
                } else {
                    std::vector<std::string> discoveredDevices = DiscoverAllTuners();        
                    if (!discoveredDevices.empty()) {
                        rec->ip = discoveredDevices[0]; 
                        tunerFound = true;
                    } 
                }

                if (tunerFound) {
                    std::string baseDir = gGlobalSaveDirectory;
                    if (!baseDir.empty() && baseDir.back() != '/') {
                        baseDir += "/";
                    }
                    
                    std::string safeTime = gScheduleList[i].startTime;
                    std::replace(safeTime.begin(), safeTime.end(), ':', '-');
            
                    std::string safeTitle = gScheduleList[i].showTitle;
                    if (safeTitle.empty()) {
                        safeTitle = "Unknown_Show";
                    } else {
                        std::replace(safeTitle.begin(), safeTitle.end(), ' ', '_');
                        std::replace(safeTitle.begin(), safeTitle.end(), '/', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), ':', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '*', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '?', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '"', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '<', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '>', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '|', '-');
                    }

                    rec->path = baseDir + "DVR_Record_Ch_" + rec->channel + "_" 
                              + gScheduleList[i].startDate + "_" + safeTime + "_"
                              + safeTitle + "_" 
                              + gScheduleList[i].duration + "s_Padded.ts";
                              
                    rec->dbIndexPosition = -1; 
            
                    thread_id worker = spawn_thread(BackgroundRecordingWorker, "DVRServiceWorker", B_NORMAL_PRIORITY, rec);
                    if (worker >= B_OK) {
                        resume_thread(worker);
                    } else {
                        delete rec;
                    }
                } else {
                    delete rec; 
                }

                gScheduleList.erase(gScheduleList.begin() + i);
                i--; 
            }
        }
        gScheduleLocker.Unlock();

        if (databaseUpdated) {
            SaveSchedulesToDisk();
        }
    }
    
    return 0;
}


class DVRServiceApp : public BApplication {
private:
    thread_id fLoopThread;
public:
    DVRServiceApp() : BApplication("application/x-vnd.haikuhdhomerun-dvr") {}
    
    DlnasHttpStreamingServer gDlnaHttp;
    DlnasDiscoveryServer     gDlnaDiscovery; 
    
    void ReadyToRun() override {
        signal(SIGPIPE, SIG_IGN);
        LoadSchedulesFromDisk();
        
        std::string ip = GetHaikuLocalIpAddress(); 
        
        // THE PORT AUTO-FALLBACK: Start at 8080, increment if busy
        int port = 8080;
        while (port < 8090) {
            int testFd = socket(AF_INET, SOCK_STREAM, 0);
            if (testFd >= 0) {
                int reuse = 1;
                setsockopt(testFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                
                struct sockaddr_in testAddr{};
                testAddr.sin_family = AF_INET;
                testAddr.sin_port = htons(port);
                testAddr.sin_addr.s_addr = INADDR_ANY;
                
                // If bind succeeds, the port is free!
                if (bind(testFd, (struct sockaddr*)&testAddr, sizeof(testAddr)) == 0) {
                    close(testFd);
                    break; 
                }
                close(testFd);
            }
            port++; // Port was busy, try the next one
        }

        gScheduleLocker.Lock();
        std::string safePathCopy = gGlobalSaveDirectory;
        gScheduleLocker.Unlock();

        // Start both engines using the validated safe port number
        gDlnaHttp.Start(port, safePathCopy, ip);
        gDlnaDiscovery.Start(ip, port);
        if (gFrontendDebugEnable) {
        	std::printf("[DVR BACKEND] DLNA Media Server initialized on http://%s:%d\n", ip.c_str(), port);
        	std::fflush(stdout); 
        }
        fLoopThread = spawn_thread(ServiceSchedulerLoop, "DVRServiceScheduler", B_LOW_PRIORITY, NULL);
        if (fLoopThread >= B_OK) {
            resume_thread(fLoopThread);
        }
    }

    
    
        void MessageReceived(BMessage* message) override {
        switch (message->what) {
 
	        case MSG_ABORT_SPECIFIC_RECORDING: {
	            // =========================================================================
	            // BACKEND SERVICE: SEARCH THE MAP AND TOGGLE THE CANCELLATION FLAG
	            // =========================================================================
	            const char* targetPath = nullptr;
	            if (message->FindString("file_path", &targetPath) == B_OK && targetPath != nullptr) {
	                std::string pathKey(targetPath);
	                
	                gRunningWorkersLocker.Lock();
	                auto iterator = gRunningWorkersMap.find(pathKey);
	                
	                if (iterator != gRunningWorkersMap.end()) {
	                    if (iterator->second.cancellationFlag != nullptr) {
	                        atomic_set(iterator->second.cancellationFlag, 1);
	                    }
	                    	                    
	                    snooze(100000); 
	                    gRunningWorkersMap.erase(iterator);
	                } else {
	                   if (gFrontendDebugEnable) printf("[DVR SERVICE ERROR] Path key was NOT found in active worker map!\n");
	                }
	                gRunningWorkersLocker.Unlock();
	            }
	            break;
	        }


               case 'bFrc': {
                const char* targetChannel = nullptr;
                const char* targetDuration = nullptr;
                const char* targetTitle = nullptr; 
                
                if (message->FindString("channel", &targetChannel) == B_OK &&
                    message->FindString("duration", &targetDuration) == B_OK) {
                    
                    std::string safeTitle = "Manual_Record";
                    if (message->FindString("show_title", &targetTitle) == B_OK && targetTitle != nullptr) {
                        safeTitle = targetTitle;
                    }
                    
                    RecordingConfig* rec = new RecordingConfig();
                    rec->channel = targetChannel;
                    rec->duration = targetDuration;
                    
                    bool tunerFound = false;
                    std::vector<std::string> discoveredDevices = DiscoverAllTuners();
                    
                    if (!discoveredDevices.empty()) {
                        rec->ip = discoveredDevices[0];
                        tunerFound = true;
                    }

	                if (tunerFound) {
	                    std::string baseDir = gGlobalSaveDirectory;
	                    if (!baseDir.empty() && baseDir.back() != '/') {
	                        baseDir += "/";
	                    }

                        time_t now = std::time(nullptr);
                        struct tm* localTimeInfo = std::localtime(&now);
                        
                        char dateBuf[32];
                        char timeBuf[32];
                        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localTimeInfo);
                        std::strftime(timeBuf, sizeof(timeBuf), "%H-%M", localTimeInfo); 

                        std::replace(safeTitle.begin(), safeTitle.end(), ' ', '_');
                        std::replace(safeTitle.begin(), safeTitle.end(), '/', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '-');
                        std::replace(safeTitle.begin(), safeTitle.end(), ':', '-');

                        rec->path = baseDir + "DVR_Record_Ch_" + rec->channel + "_" 
                                  + dateBuf + "_" + timeBuf + "_"
                                  + safeTitle + "_"
                                  + rec->duration + "s_Padded.ts";
                                  
                        rec->dbIndexPosition = -1;
                        
                        thread_id worker = spawn_thread(BackgroundRecordingWorker, "DVRServiceWorker", B_NORMAL_PRIORITY, rec);
                        if (worker >= B_OK) {
                            resume_thread(worker);
                        } else {
                            delete rec;
                        }
                    } else {
                        delete rec;
                    }
                }
                break;
            }
          
            
            default:
                BApplication::MessageReceived(message);
                break;
        }
    }
    
    bool QuitRequested() override {     
	    atomic_set(&gStopService, 1);
	    atomic_set(&gCancelRecording, 1);        
	    return true; 
	}
};

void SignalExitInterceptor(int signalType) { 
    atomic_set(&gStopService, 1);
    atomic_set(&gCancelRecording, 1);
    
    DVRServiceApp* app = dynamic_cast<DVRServiceApp*>(be_app);
    if (app) {
        app->gDlnaDiscovery.Stop();
    }
}

int main() {
    DVRServiceApp app;
    app.Run();
    return 0;
}

