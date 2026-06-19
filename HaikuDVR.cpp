/*
 * Copyright 2026, Kris Beazley HaikuDVR@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <TextControl.h>
#include <StringView.h>
#include <MenuField.h>
#include <MenuBar.h>
#include <Box.h>
#include <Bitmap.h>
#include <TranslationUtils.h>
#include <Roster.h>   
#include <private/app/LaunchRoster.h>
#include <PopUpMenu.h>
#include <CalendarView.h>
#include <MenuItem.h>
#include <Entry.h>
#include <Path.h>
#include <Volume.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <FilePanel.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h> 
#include <OS.h>
#include <curl/curl.h>
#include <fstream>
#include <string>
#include <vector>
#include <View.h>
#include <SplitView.h>
#include <TextView.h>
#include <LayoutBuilder.h>   
#include <String.h>
#include <ctime>
#include <nlohmann/json.hpp>
#include "hdhomerun.h"
#include <Debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <Notification.h>
#include <Alert.h>





const uint32 MSG_CHECK_FIRMWARE 			= 'chfw';
const uint32 MSG_FIRMWARE_CHECK_DONE 		= 'fwdn';
const uint32 MSG_PREFILL_RECORD_SCHEDULE 	= 'pfrs';
const uint32 MSG_RESTART_BACKEND   			= 'rstB';
const uint32 MSG_POLL_BACKEND      			= 'polB';
const uint32 MSG_POPUP_CALENDAR    			= 'popC';
const uint32 MSG_DATE_SELECTED     			= 'dtSl';
const uint32 MSG_START_RECORDING   			= 'recS';
const uint32 MSG_STOP_RECORDING    			= 'recT';
const uint32 MSG_RECORDING_DONE    			= 'recD';
const uint32 MSG_TUNER_SELECTED    			= 'tunS';
const uint32 MSG_ADD_SCHEDULE      			= 'schA';
const uint32 MSG_DURATION_SELECTED 			= 'durS';
const uint32 MSG_CHANNEL_CLICKED   			= 'chCl';
const uint32 MSG_REFRESH_SCHEDULES 			= 'schR';
const uint32 MSG_PLAY_IN_MEDIAPLAYER 		= 'pimp';
const uint32 MSG_REMOVE_SCHEDULE   			= 'scRh';
const uint32 MSG_FILTER_ALL        			= 'fltA';
const uint32 MSG_FILTER_HD         			= 'fltH';
const uint32 MSG_FILTER_SD         			= 'fltS';
const uint32 MSG_CHOOSE_DIR        			= 'chDr';
const uint32 MSG_DIR_CHOSEN        			= 'drCh';
const uint32 MSG_COUNTDOWN_TICK    			= 'cdTk';
const uint32 MSG_CLOCK_UP          			= 'clkU';
const uint32 MSG_CLOCK_DOWN    	   			= 'clkD';
const uint32 MSG_DISK_SPACE_WARNING 	    = 'dSpc';
const uint32 MSG_REFRESH_CHANNEL_LIST_ICONS = 'rIco';
const uint32 MSG_STREAM_PROGRESS_UPDATE     = 'sPrg';
const uint32 MSG_TOGGLE_NOTIFICATIONS       = 'ntfg';
const uint32 MSG_TOGGLE_DEBUG               = 'dbug';
const uint32 MSG_OPEN_GUIDE 				= 'opgd';



using json = nlohmann::json;
const char* kSettingsFilePath = "/boot/home/config/settings/HaikuDVR_schedules.json";

void ensure_config_dir() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("HaikuDVR/icons");
        create_directory(path.Path(), 0755);
    }
}


struct AppConfig {
    bool showUpdateNotifications = true;
    bool debugEnable = true; 
};

AppConfig cfg; 


int32 gCancelRecording = 0;
int32 gStopScheduler = 0;

// =========================================================================
//  DATA STORAGE STRUCTS 
// =========================================================================

struct RecordingConfig {
    BMessenger windowMessenger;
    std::string ip;
    std::string channel;
    std::string duration;
    std::string path;
    int32 dbIndexPosition;
};

struct ScheduleItem {
    std::string startDate; 
    std::string startTime; 
    std::string channel;
    std::string duration; 
    bool processed;
    std::string tunerIp; 
};

struct UpcomingShowItem {
    std::string title;
    std::string startTimeStr; 
    int32 durationMinutes; 
};

struct ChannelGuideItem {
    std::string guideNumber;
    std::string guideName;
    std::string nowPlaying; 
    int32 nowPlayingDurationMinutes; 
    std::vector<UpcomingShowItem> futureLineup; 
};

struct GuideProgramBlock {
    BString title;
    BString timeDisplay;
    float cellWidthPixels; 
    int32 durationMinutes; 
};

struct GuideRowModel {
    BString channelLabel;
    const BBitmap* channelIcon;
    std::vector<GuideProgramBlock> programs;
};


std::string gGlobalSaveDirectory = "/boot/home";
std::vector<ScheduleItem> gScheduleList;
BLocker gScheduleLocker; 

void SaveSchedulesToDisk() {
    gScheduleLocker.Lock();
    
    json jRoot = json::object();
    
    jRoot["save_directory"] = gGlobalSaveDirectory; 
    
    // --- MAP STRUCT VALUES TO JSON ---
    jRoot["show_update_notifications"] = cfg.showUpdateNotifications; 
    jRoot["debug_enable"]              = cfg.debugEnable;
    
    json jSchedules = json::array();
    for (const auto& item : gScheduleList) {
        if (!item.processed) {
            jSchedules.push_back({
                {"date", item.startDate}, 
                {"time", item.startTime},
                {"channel", item.channel},
                {"duration", item.duration},
                {"tuner_ip", item.tunerIp} 
            });
        }
    }
    jRoot["schedules"] = jSchedules;
    gScheduleLocker.Unlock();

    std::ofstream file(kSettingsFilePath);
    if (file.is_open()) {
        file << jRoot.dump(4);
        file.close();
    }
}



void LoadSchedulesFromDisk() {
    std::ifstream file(kSettingsFilePath);
    if (!file.is_open()) return;

    try {
        json jIn;
        file >> jIn;
        gScheduleLocker.Lock();
        
        if (jIn.is_object()) {
            // Read directories and configurations from the root object
            gGlobalSaveDirectory        = jIn.value("save_directory", "/boot/home");
            cfg.showUpdateNotifications = jIn.value("show_update_notifications", true);
            cfg.debugEnable             = jIn.value("debug_enable", true);
            
            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate = entry.value("date", "2026-06-13"); 
                    item.startTime = entry.value("time", "12:00");
                    item.channel   = entry.value("channel", "5.1");
                    item.duration  = entry.value("duration", "1800");
                    item.processed = false;
                    gScheduleList.push_back(item);
                }
            }
        }
        else if (jIn.is_array()) {
            // Safe legacy fallback if your file only contains a raw array of schedules
            cfg.showUpdateNotifications = true;
            cfg.debugEnable             = true;
            
            gScheduleList.clear();
            for (const auto& entry : jIn) {
                ScheduleItem item;
                item.startDate = entry.value("date", "2026-06-13"); 
                item.startTime = entry.value("time", "12:00");
                item.channel   = entry.value("channel", "5.1");
                item.duration  = entry.value("duration", "1800");
                item.processed = false;
                gScheduleList.push_back(item);
            }
        }
        
        gScheduleLocker.Unlock();
    } catch (...) {
        gScheduleLocker.Unlock();
    }
    file.close();
}


namespace AppInfo {
    static const char* const VERSION_STRING = "HaikuDVR v1.0.8 (Haiku OS)";
}


// =============================================================================
// NATIVE ASYNCHRONOUS UPDATE ENGINE IMPLEMENTATION (CURL ENGINE PASS)
// =============================================================================
static int32 BackgroundUpdateChecker(void* data) {
    // Wait a brief 5 seconds after application boot to allow UI rendering to finalize completely
    snooze(5000000); 

    if (cfg.debugEnable) printf("[DEBUG_UPDATE] Asynchronous curl update checker running...\n");

    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/HaikuDVR/refs/heads/main/VERSION";

    BString shellCmdString;
    shellCmdString.SetToFormat("curl -sL \"%s\"", targetUrl);

    BString remoteVersionStr = "";
    
    FILE* pipeStream = popen(shellCmdString.String(), "r");
    if (pipeStream != nullptr) {
        char buffer[128] = {0};
        if (fgets(buffer, sizeof(buffer), pipeStream) != nullptr) {
            remoteVersionStr = buffer;
        }
        pclose(pipeStream);
    }

    remoteVersionStr.Trim(); 
    if (cfg.debugEnable) printf("[DEBUG_UPDATE] Raw text received from GitHub: '%s'\n", remoteVersionStr.String());

    remoteVersionStr.Trim(); 
    if (cfg.debugEnable) printf("[DEBUG_UPDATE] Raw text received from GitHub: '%s'\n", remoteVersionStr.String());
    
    if (remoteVersionStr.Length() > 0) {
        BString currentVersionStr = AppInfo::VERSION_STRING;
        if (cfg.debugEnable) printf("[DEBUG_UPDATE] Local AppInfo text before cleaning: '%s'\n", currentVersionStr.String());

        int32 curMajor = 0, curMinor = 0, curRevision = 0;
        int32 remMajor = 0, remMinor = 0, remRevision = 0;


        if (sscanf(currentVersionStr.String(), "%*[^v]v%d.%d.%d", &curMajor, &curMinor, &curRevision) != 3) {
            sscanf(currentVersionStr.String(), "%*[^0-9]%d.%d.%d", &curMajor, &curMinor, &curRevision);
        }

        if (sscanf(remoteVersionStr.String(), "%*[^v]v%d.%d.%d", &remMajor, &remMinor, &remRevision) != 3) {
            sscanf(remoteVersionStr.String(), "%*[^0-9]%d.%d.%d", &remMajor, &remMinor, &remRevision);
        }

        if (cfg.debugEnable) {
            printf("[DEBUG_UPDATE] Cleaned local target string: '%d.%d.%d'\n", curMajor, curMinor, curRevision);
        }

        int32 currentFlattened = (curMajor * 10000) + (curMinor * 100) + curRevision;
        int32 remoteFlattened  = (remMajor * 10000) + (remMinor * 100) + remRevision;

        if (cfg.debugEnable) {
            printf("[DEBUG_UPDATE] Calculated values for math match -> Local: %d | Remote: %d\n", 
                   (int)currentFlattened, (int)remoteFlattened);
        }


        if (remoteFlattened > currentFlattened) {
            if (cfg.debugEnable) printf("[DEBUG_UPDATE] Update matched! Checking alert preference flags...\n");

            if (!cfg.showUpdateNotifications) {
                if (cfg.debugEnable) printf("[DEBUG_UPDATE] Suppressing desktop alert toast\n");
                return B_OK; 
            }

            BNotification updateAlert(B_INFORMATION_NOTIFICATION);
            updateAlert.SetGroup("HaikuDVR");
            updateAlert.SetTitle("Update Available");
            
            BString alertContent;
            alertContent << "A newer version of HaikuDVR is available! (" << remoteVersionStr 
                         << ")";
            updateAlert.SetContent(alertContent.String());
            
            updateAlert.Send();
            if (cfg.debugEnable) printf("[DEBUG_UPDATE] Toast notification sent successfully.\n");
        } else {
            if (cfg.debugEnable) printf("[DEBUG_UPDATE] Math complete: Client binary is already completely up to date.\n");
        }
    } else {
        if (cfg.debugEnable) printf("[DEBUG_UPDATE] CRITICAL ERR: Raw text data read from pipe buffer was empty!\n");
    }
    
    return B_OK;
}


// =========================================================================
// FORWARD DECLARATIONS (Fixes compiler scanning scopes)
// =========================================================================
class DVRWindow; // Tells the compiler DVRWindow exists lower down
extern size_t NetworkStringCallback(void* contents, size_t size, size_t nmemb, void* userp);
// 1. Add the forward declaration near the top of the file (around line 1056)
class RealTVGuideWindow; 

// =========================================================================
// PACKET PACKER FOR FIRMWARE WORKER
// =========================================================================
struct FirmwareParam {
    BWindow* targetWindow;
    BString tunerIp;
};


// =========================================================================
// ASYNCHRONOUS BACKGROUND TUNER FIRMWARE CHECKER THREAD
// =========================================================================
static int32 FirmwareCheckerWorker(void* data) {
    // Unpack our structured parameter packet safely
    FirmwareParam* param = (FirmwareParam*)data;
    if (param == nullptr) return B_ERROR;
    
    BWindow* window = param->targetWindow;
    BString targetIp = param->tunerIp;
    
    // Clean up the parameter allocation right away since we have copies of its values
    delete param;

    if (window == nullptr) return B_ERROR;

    // 1. FIXED: Pass the exact target IP string instead of "AUTO"!
    // If your app hasn't selected an IP yet, fall back to "AUTO" mode.
    const char* discoveryTarget = targetIp.IsEmpty() ? "AUTO" : targetIp.String();

    struct hdhomerun_device_t* hdDevice = hdhomerun_device_create_from_str(discoveryTarget, NULL);
    if (hdDevice == nullptr) {
        BMessage notifyMsg(MSG_FIRMWARE_CHECK_DONE);
        notifyMsg.AddString("status_text", "Firmware Error: No HDHomeRun hardware detected at target IP.");
        window->PostMessage(&notifyMsg);
        return B_OK;
    }

    // 2. Fetch current firmware version
    char* activeVersionStr = nullptr;
    int versionResult = hdhomerun_device_get_version(hdDevice, &activeVersionStr, NULL);

    if (versionResult < 0 || activeVersionStr == nullptr) {
        BMessage notifyMsg(MSG_FIRMWARE_CHECK_DONE);
        notifyMsg.AddString("status_text", "Firmware Error: Failed to retrieve internal tuner version.");
        window->PostMessage(&notifyMsg);
        hdhomerun_device_destroy(hdDevice);
        return B_OK;
    }

    BString currentFirmware(activeVersionStr);

    // 3. Query the latest stable firmware package version tracking ledger using libcurl
    std::string cloudPayload;
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://silicondust.com");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cloudPayload);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    BString latestFirmware = "";
    try {
        auto jFirmware = json::parse(cloudPayload);
        if (jFirmware.is_object() && jFirmware.contains("upgrade")) {
            latestFirmware = jFirmware["upgrade"].value("version", "").c_str();
        }
    } catch (...) {}

    if (latestFirmware.IsEmpty()) {
        latestFirmware = currentFirmware; 
    }

    // 4. Assemble the package message to deliver final results back to the master window thread
    BMessage finishMessage(MSG_FIRMWARE_CHECK_DONE);
    finishMessage.AddString("current_version", currentFirmware.String());
    finishMessage.AddString("latest_version", latestFirmware.String());
    
    window->PostMessage(&finishMessage);
    hdhomerun_device_destroy(hdDevice);
    return B_OK;
}






size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
struct AsyncIconDownloadConfig {
    BMessenger windowMessenger;
    std::string iconPath;
    std::string downloadUrl;
    int32 listRowIndex;
};

// This function executes automatically as fast as media packets stream over the air
int StreamProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (atomic_get(&gCancelRecording) == 1) {
        return 1; 
    }

    RecordingConfig* config = static_cast<RecordingConfig*>(clientp);
    if (!config || !config->windowMessenger.IsValid()) return 0;
    static std::time_t lastUpdate = 0;
    std::time_t currentTime = std::time(nullptr);
    if (currentTime - lastUpdate >= 1) {
        lastUpdate = currentTime;
        double megabytesDownloaded = static_cast<double>(dlnow) / (1024.0 * 1024.0);
        BMessage progressMsg(MSG_STREAM_PROGRESS_UPDATE);
        progressMsg.AddDouble("bytes_now", megabytesDownloaded);
        config->windowMessenger.SendMessage(&progressMsg);
        config->windowMessenger.SendMessage(MSG_COUNTDOWN_TICK);
    }

    return 0;
}



struct DownloadQueueItem {
    std::string iconPath;
    std::string downloadUrl;
    int32 listRowIndex;
};

// Global queue parameters
std::vector<DownloadQueueItem> gIconDownloadQueue;
BLocker gIconQueueLocker;
int32 gIconThreadRunning = 0;
BMessenger* gIconWindowMessenger = nullptr;


int32 SerialIconDownloaderThread(void* data) {
    // Forward declaration to link cURL callbacks cleanly
    size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    atomic_set(&gIconThreadRunning, 1);

    while (true) {
        DownloadQueueItem job;
        bool hasJob = false;

        // Extract the next item from the queue safely
        gIconQueueLocker.Lock();
        if (!gIconDownloadQueue.empty()) {
            job = gIconDownloadQueue.front();
            gIconDownloadQueue.erase(gIconDownloadQueue.begin());
            hasJob = true;
        }
        gIconQueueLocker.Unlock();

        // If the queue is empty, exit the thread cleanly
        if (!hasJob) break;

        CURL* downloadCurl = curl_easy_init();
        if (downloadCurl) {
            std::ofstream iconOut(job.iconPath.c_str(), std::ios::binary);
            if (iconOut.is_open()) {
                curl_easy_setopt(downloadCurl, CURLOPT_URL, job.downloadUrl.c_str());
                curl_easy_setopt(downloadCurl, CURLOPT_WRITEFUNCTION, StorageWriteCallback);
                curl_easy_setopt(downloadCurl, CURLOPT_WRITEDATA, &iconOut);
                curl_easy_setopt(downloadCurl, CURLOPT_TIMEOUT, 4L);
                
                CURLcode res = curl_easy_perform(downloadCurl);
                iconOut.close();
                
                if (res == CURLE_OK && gIconWindowMessenger && gIconWindowMessenger->IsValid()) {
                    BMessage completionMsg(MSG_REFRESH_CHANNEL_LIST_ICONS);
                    completionMsg.AddInt32("row_index", job.listRowIndex);
                    gIconWindowMessenger->SendMessage(&completionMsg);
                } else {
                    std::remove(job.iconPath.c_str()); 
                }
            }
            curl_easy_cleanup(downloadCurl);
        }
        snooze(50000); // 50ms brief pause prevents hammering network hardware channels
    }

    atomic_set(&gIconThreadRunning, 0);
    return 0;
}



class ScheduleListView : public BListView {
public:
    ScheduleListView(BRect frame, const char* name, list_view_type type = B_SINGLE_SELECTION_LIST)
        : BListView(frame, name, type) {}

    void MouseDown(BPoint point) override {
        BMessage* msg = Window()->CurrentMessage();
        int32 buttons = 0;
        
        if (msg->FindInt32("buttons", &buttons) == B_OK && buttons == B_SECONDARY_MOUSE_BUTTON) {
            int32 index = IndexOf(point);
            if (index >= 0) {
                Select(index); 
                
                BPopUpMenu* contextMenu = new BPopUpMenu("Context", false, false);
                BMenuItem* deleteItem = new BMenuItem("Delete Schedule", NULL);
                contextMenu->AddItem(deleteItem);
                
                BPoint screenPoint = ConvertToScreen(point) + BPoint(2, 2);
                BMenuItem* selectedItem = contextMenu->Go(screenPoint, false, true, false);
                
                if (selectedItem == deleteItem) {
                    BMessage removeMsg(MSG_REMOVE_SCHEDULE);
                    removeMsg.AddInt32("list_index", index);
                    Window()->PostMessage(&removeMsg);
                }
            }
        } else {
            BListView::MouseDown(point);
        }
    }
};


size_t NetworkStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* s = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    if (atomic_get(&gCancelRecording) == 1) {
        return 0;
    }
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    file->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    RecordingConfig* config = static_cast<RecordingConfig*>(clientp);
    if (atomic_get(&gCancelRecording) == 1) return 1;
    config->windowMessenger.SendMessage(MSG_COUNTDOWN_TICK);
    snooze(1000000); 
    return 0;
}




int32 NetworkRecordingThread(void* data) {
    RecordingConfig* config = static_cast<RecordingConfig*>(data);
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string url = "http://" + config->ip + ":5004/auto/v" + config->channel + "?duration=" + config->duration;
        std::ofstream outputFile(config->path, std::ios::binary);
        if (outputFile.is_open()) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StorageWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outputFile);
            
            // Turn on modern 64-bit progress tracking engine definitions
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);            
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, StreamProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, config);

            curl_easy_perform(curl);
            outputFile.close();
        }
        curl_easy_cleanup(curl);
    }
    config->windowMessenger.SendMessage(MSG_RECORDING_DONE);
    delete config;
    return 0;
}


int32 ClockSchedulerThread(void* data) {
    BMessenger* windowMessenger = static_cast<BMessenger*>(data);
    int pulseTickCounter = 0;
    int diskCheckCounter = 0;

    while (atomic_get(&gStopScheduler) == 0) {
        snooze(1000000); 
        
        pulseTickCounter++;
        if (pulseTickCounter >= 2) {
            pulseTickCounter = 0; 
            if (windowMessenger && windowMessenger->IsValid()) {
                windowMessenger->SendMessage(MSG_POLL_BACKEND);
            }
        }

        diskCheckCounter++;
        if (diskCheckCounter >= 5) {
            diskCheckCounter = 0;
            BDirectory dir(gGlobalSaveDirectory.c_str());
            BVolume volume;
            
            if (dir.InitCheck() == B_OK && dir.GetVolume(&volume) == B_OK) {
                off_t freeBytes = volume.FreeBytes();
                off_t minimumRequiredBytes = 5368709120LL; 

                if (freeBytes < minimumRequiredBytes && windowMessenger && windowMessenger->IsValid()) {
                    int32 freeMegabytes = static_cast<int32>(freeBytes / (1024 * 1024));
                    BMessage spaceAlert(MSG_DISK_SPACE_WARNING);
                    spaceAlert.AddInt32("free_mb", freeMegabytes);
                    windowMessenger->SendMessage(&spaceAlert);
                }
            }
        }
    }
    delete windowMessenger;
    return 0;
}





// =========================================================================
// STATIC TIMELINE HEADER VIEW 
// =========================================================================
class TimelineHeaderView : public BView {
public:
    TimelineHeaderView(BRect frame) : BView(frame, "timelineHeader", B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW) {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void Draw(BRect updateRect) override {
        BRect bounds = Bounds();
        rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
        rgb_color gridLineColor = ui_color(B_CONTROL_BORDER_COLOR);
        
        const float kHeaderWidth = 300.0;
        const float kCellWidth = 350.0;

        // Draw background accent
        SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        FillRect(bounds);

        // Print "CHANNELS" header label over the logo column slot
        SetHighColor(textColor);
        BFont labelFont;
        GetFont(&labelFont);
        labelFont.SetFace(B_BOLD_FACE);
        labelFont.SetSize(10.0);
        SetFont(&labelFont);
        MovePenTo(16.0, bounds.top + 24);
        DrawString("CHANNELS");

        // Vertical dividing track line separation rule
        SetHighColor(gridLineColor);
        StrokeLine(BPoint(kHeaderWidth, bounds.top), BPoint(kHeaderWidth, bounds.bottom));

        // Generate Time Indicator Column Offsets
        float currentLeft = kHeaderWidth + 1.0;
        const char* timeIntervals[] = { "CURRENT TIME", "+ 30 MINS", "+ 1.0 HOUR", "+ 1.5 HOURS" };

        for (int i = 0; i < 4; i++) {
            SetHighColor(textColor);
            MovePenTo(currentLeft + 20, bounds.top + 24);
            DrawString(timeIntervals[i]);

            // Visual tracking grid column ticks
            SetHighColor(gridLineColor);
            StrokeLine(BPoint(currentLeft + kCellWidth - 12, bounds.top + 8), 
                       BPoint(currentLeft + kCellWidth - 12, bounds.bottom));
            
            currentLeft += kCellWidth;
        }

        // Draw bottom structural bounding line
        SetHighColor(gridLineColor);
        StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
    }
};



// =========================================================================
// 2. CHANNEL LIST ITEM 
// =========================================================================
class ChannelListItem : public BListItem {
public:
    std::string textDisplay;
    const BBitmap* channelIcon;

    ChannelListItem(const char* text, const BBitmap* cachedIcon) : BListItem() {
        textDisplay = text;
        channelIcon = cachedIcon;
    }

    virtual ~ChannelListItem() {
    }

    void DrawItem(BView* owner, BRect itemRect, bool drawEverything) override {
        if (IsSelected() || drawEverything) {
            rgb_color bgColor;
            if (IsSelected()) {
                bgColor = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);
                owner->SetHighColor(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR));
            } else {
                bgColor = owner->ViewColor();
                owner->SetHighColor(owner->HighColor());
            }
            owner->SetLowColor(bgColor);
            owner->FillRect(itemRect, B_SOLID_LOW);
        }

        float iconOffset = 5.0;
        if (channelIcon != nullptr) {
            BRect destRect(itemRect.left + 5, itemRect.top + 1, itemRect.left + 27, itemRect.top + 23);            
            drawing_mode oldMode = owner->DrawingMode();
            owner->SetDrawingMode(B_OP_ALPHA);            
            owner->DrawBitmap(channelIcon, channelIcon->Bounds(), destRect, B_FILTER_BITMAP_BILINEAR);            
            owner->SetDrawingMode(oldMode);            
            iconOffset = 32.0; 
        }
        owner->MovePenTo(itemRect.left + iconOffset, itemRect.top + 16);
        owner->DrawString(textDisplay.c_str());

    }

};



// =========================================================================
// UPDATED LIST ITEM WITH HOVER TRACKING AND CELL CLICK INTERFACE
// =========================================================================
class GuideListRowItem : public BListItem {
public:
    GuideRowModel fData;
    int32 fRowIndex;
    int32 fHoveredCellIndex; // Tracks which show block is currently hovered

    GuideListRowItem(GuideRowModel data, int32 index) 
        : BListItem(), fData(data), fRowIndex(index), fHoveredCellIndex(-1) {
        SetHeight(140); 
    }

    void Update(BView* owner, const BFont* font) override {
        BListItem::Update(owner, font);
        SetHeight(140); 
    }

    // Call this custom method from the parent window's MouseMoved engine
    void TrackMouseHover(BView* owner, BPoint point, BRect itemRect) {
        const float kHeaderWidth = 300.0;
        const float kCellWidth = 350.0;

        if (!itemRect.Contains(point)) {
            if (fHoveredCellIndex != -1) {
                fHoveredCellIndex = -1;
                owner->Invalidate(itemRect);
            }
            return;
        }

        float localX = point.x - itemRect.left;
        int32 cellIndex = -1;

        if (localX > kHeaderWidth) {
            cellIndex = (int32)((localX - kHeaderWidth) / kCellWidth);
            if (cellIndex >= (int32)fData.programs.size()) cellIndex = -1;
        }

        if (fHoveredCellIndex != cellIndex) {
            fHoveredCellIndex = cellIndex;
            owner->Invalidate(itemRect); // Instantly redraw to show highlight shift
        }
    }

    void HandleDoubleClick(BView* owner, BPoint point, BRect itemRect, BWindow* parentWindow) {
        const float kHeaderWidth = 300.0;
        const float kCellWidth = 350.0;

        float localX = point.x - itemRect.left;
        if (localX > kHeaderWidth) {
            int32 cellIndex = (int32)((localX - kHeaderWidth) / kCellWidth);
            if (cellIndex >= 0 && cellIndex < (int32)fData.programs.size()) {
                const auto& selectedProg = fData.programs[cellIndex];

                // Create a clean background message packet
                BMessage selectionBroadcast(MSG_PREFILL_RECORD_SCHEDULE);
                selectionBroadcast.AddString("show_title", selectedProg.title.String());
                selectionBroadcast.AddString("start_time", selectedProg.timeDisplay.String());
                selectionBroadcast.AddString("channel_label", fData.channelLabel.String());
                selectionBroadcast.AddInt32("duration_minutes", selectedProg.durationMinutes);
                
                // Package the numeric subchannel prefix safely
                BString targetSubchannel = fData.channelLabel;
                int32 spaceIndex = targetSubchannel.FindFirst(" ");
                if (spaceIndex != B_ERROR) {
                    targetSubchannel.Truncate(spaceIndex);
                }
                targetSubchannel.Trim();
                selectionBroadcast.AddString("numeric_subchannel", targetSubchannel.String());

                // Post the message up to your main view controller loop seamlessly!
                if (parentWindow != nullptr) {
                    parentWindow->PostMessage(&selectionBroadcast);
                }
            }
        }
    }



      void DrawItem(BView* owner, BRect itemRect, bool drawEverything) override {
        rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
        rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
        rgb_color gridLineColor = ui_color(B_CONTROL_BORDER_COLOR);
        rgb_color cellBgColor = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
        
        owner->SetHighColor(panelBg);
        owner->FillRect(itemRect);

        const float kHeaderWidth = 300.0; 
        const float kCellWidth = 350.0;

        // Draw Channel Column Backdrop
        BRect headerRect(itemRect.left, itemRect.top, itemRect.left + kHeaderWidth, itemRect.bottom - 1);
        owner->SetHighColor(panelBg);
        owner->FillRect(headerRect);
        
        // Render 98x98 Channel Icon
        float iconOffset = 16.0;
        if (fData.channelIcon != nullptr) {
            float topOffset = itemRect.top + 21.0; 
            BRect destRect(itemRect.left + 16, topOffset, itemRect.left + 114, topOffset + 98);
            
            drawing_mode oldMode = owner->DrawingMode();
            owner->SetDrawingMode(B_OP_ALPHA);
            owner->DrawBitmap(fData.channelIcon, fData.channelIcon->Bounds(), destRect, B_FILTER_BITMAP_BILINEAR);
            owner->SetDrawingMode(oldMode);
            iconOffset = 130.0; 
        }
        
        owner->SetHighColor(textColor);
        owner->SetLowColor(panelBg);
        owner->MovePenTo(itemRect.left + iconOffset, itemRect.top + 74);
        
        BString truncatedChannelName = fData.channelLabel;
        owner->TruncateString(&truncatedChannelName, B_TRUNCATE_END, (kHeaderWidth - 16.0) - iconOffset);
        owner->DrawString(truncatedChannelName.String());
        
        owner->SetHighColor(gridLineColor);
        owner->StrokeLine(BPoint(itemRect.left + kHeaderWidth, itemRect.top), BPoint(itemRect.left + kHeaderWidth, itemRect.bottom));

        // =========================================================================
        // PASS 1: RENDER MATRIX CARD BODY AND BACKGROUNDS FOR ALL COLUMNS FIRST
        // =========================================================================
        float currentLeft = itemRect.left + kHeaderWidth + 1.0;
        for (size_t idx = 0; idx < fData.programs.size(); idx++) {
            BRect cellRect(currentLeft, itemRect.top + 12, currentLeft + kCellWidth - 12, itemRect.bottom - 13);
            
            owner->SetHighColor(cellBgColor);
            owner->FillRect(cellRect);
            
            if ((int32)idx == fHoveredCellIndex) {
                owner->SetHighColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
                owner->StrokeRect(cellRect);
                BRect innerRect = cellRect.InsetByCopy(1.0, 1.0);
                owner->StrokeRect(innerRect);
            } else {
                owner->SetHighColor(gridLineColor);
                owner->StrokeRect(cellRect);
            }
            
            owner->SetHighColor(textColor);
            owner->SetLowColor(cellBgColor);
            
            BFont titleFont;
            owner->GetFont(&titleFont);
            titleFont.SetSize(11.0);
            owner->SetFont(&titleFont);
            
            owner->MovePenTo(cellRect.left + 20, cellRect.top + 36);
            owner->DrawString(fData.programs[idx].timeDisplay.String());
            
            owner->MovePenTo(cellRect.left + 20, cellRect.top + 70);
            BString truncatedTitle = fData.programs[idx].title;
            owner->TruncateString(&truncatedTitle, B_TRUNCATE_END, cellRect.Width() - 32);
            owner->DrawString(truncatedTitle.String());
            
            currentLeft += kCellWidth;
        }

        // =========================================================================
        // PASS 2: OVERLAY RED COLORS & BADGES ON SCHEDULED ITEMS
        // =========================================================================
        currentLeft = itemRect.left + kHeaderWidth + 1.0;
        BString cleanNumberOnly = fData.channelLabel;
        int32 spaceIndex = cleanNumberOnly.FindFirst(" ");
        if (spaceIndex != B_ERROR) { cleanNumberOnly.Truncate(spaceIndex); }
        cleanNumberOnly.Trim();
        std::string targetChannel = cleanNumberOnly.String();

        gScheduleLocker.Lock();
        
        for (size_t idx = 0; idx < fData.programs.size(); idx++) {
            const auto& prog = fData.programs[idx];
            BRect cellRect(currentLeft, itemRect.top + 12, currentLeft + kCellWidth - 12, itemRect.bottom - 13);

            int cellHour = -1, cellMin = -1;
            std::string cellTimeStr = prog.timeDisplay.String();

            if (cellTimeStr == "LIVE NOW") {
                time_t now = real_time_clock();
                struct tm* timeInfo = localtime(&now);
                cellHour = timeInfo->tm_hour;
                cellMin = timeInfo->tm_min; 
            } else {
                char ampm[16] = {0};
                int h = 0, m = 0;
                if (sscanf(cellTimeStr.c_str(), "%d:%d %15s", &h, &m, ampm) >= 2) {
                    std::string aStr(ampm);
                    if (aStr.find("PM") != std::string::npos || aStr.find("pm") != std::string::npos) {
                        if (h < 12) h += 12;
                    } else if (aStr.find("AM") != std::string::npos || aStr.find("am") != std::string::npos) {
                        if (h == 12) h = 0;
                    }
                    cellHour = h;
                    cellMin = m;
                }
            }

            int cellAbsoluteMinutes = (cellHour * 60) + cellMin;
            bool isScheduled = false;

            for (size_t i = 0; i < gScheduleList.size(); i++) {
                if (!gScheduleList[i].processed && gScheduleList[i].channel.find(targetChannel) != std::string::npos) {
                    int schedHour = -1, schedMin = -1;
                    if (sscanf(gScheduleList[i].startTime.c_str(), "%d:%d", &schedHour, &schedMin) == 2) {
                        int schedStartAbsolute = (schedHour * 60) + schedMin;
                        
                        int durationSeconds = 0;
                        try {
                            durationSeconds = std::stoi(gScheduleList[i].duration);
                        } catch (...) {
                            durationSeconds = 1800; 
                        }
                        int schedDurationMinutes = durationSeconds / 60;
                        int schedEndAbsolute = schedStartAbsolute + schedDurationMinutes;

                        int searchTime = cellAbsoluteMinutes + 5;

                        if (searchTime >= schedStartAbsolute && cellAbsoluteMinutes < (schedEndAbsolute - 1)) {
                            isScheduled = true;
                            break;
                        }
                    }
                }
            }

            if (isScheduled) {
                owner->PushState(); 
                
                // 1. Fill background with a clean burgundy tint
                rgb_color scheduledBgColor = { 75, 20, 20, 255 }; 
                owner->SetHighColor(scheduledBgColor);
                owner->FillRect(cellRect.InsetByCopy(1.0, 1.0));

                // 2. Trace bold crimson border outline frame (skip if currently hovered to preserve accent colors)
                if ((int32)idx != fHoveredCellIndex) {
                    rgb_color borderRed = { 220, 40, 40, 255 };
                    owner->SetHighColor(borderRed);
                    owner->StrokeRect(cellRect);
                    owner->StrokeRect(cellRect.InsetByCopy(1.0, 1.0));
                }

                // 3. Redraw program strings over the newly painted backdrop area
                owner->SetLowColor(scheduledBgColor);
                
                BFont textFont;
                owner->GetFont(&textFont);
                textFont.SetSize(11.0);
                owner->SetFont(&textFont);

                // High-contrast pinkish-red for time string boundary text
                owner->SetHighColor(255, 140, 140, 255);
                owner->MovePenTo(cellRect.left + 20, cellRect.top + 36);
                owner->DrawString(prog.timeDisplay.String());

                // Bright white for title text readability
                owner->SetHighColor(255, 255, 255, 255);
                owner->MovePenTo(cellRect.left + 20, cellRect.top + 70);
                BString truncatedTitle = prog.title;
                owner->TruncateString(&truncatedTitle, B_TRUNCATE_END, cellRect.Width() - 32);
                owner->DrawString(truncatedTitle.String());

                // 4. Draw the bold [REC] badge
                owner->SetHighColor(255, 60, 60, 255);
                BFont badgeFont;
                owner->GetFont(&badgeFont);
                badgeFont.SetFace(B_BOLD_FACE);
                badgeFont.SetSize(11.0);
                owner->SetFont(&badgeFont);
                
                float badgeX = cellRect.right - owner->StringWidth("[REC]") - 20;
                float badgeY = cellRect.top + 36; 
                owner->MovePenTo(badgeX, badgeY);
                owner->DrawString("[REC]");

                owner->PopState(); 
            }

            currentLeft += kCellWidth;
        }
        gScheduleLocker.Unlock();

        owner->SetHighColor(gridLineColor);
        owner->StrokeLine(BPoint(itemRect.left, itemRect.bottom), BPoint(itemRect.right, itemRect.bottom));
    }



};

// =========================================================================
// CUSTOM LISTVIEW INTERCEPTOR ROUTING MOUSE EVENTS TO ITEM CARDS
// =========================================================================
class InteractiveGuideListView : public BListView {
private:
    BWindow* fParentShortcutTarget;
public:
    InteractiveGuideListView(const char* name, BWindow* mainAppWindow) 
        : BListView(name, B_SINGLE_SELECTION_LIST), fParentShortcutTarget(mainAppWindow) {
        // Enforce mouse movement reporting flags
        SetFlags(Flags() | B_POINTER_EVENTS);
    }

    void MouseMoved(BPoint point, uint32 transit, const BMessage* message) override {
        BListView::MouseMoved(point, transit, message);
        
        // Cycle down items and push coordinate tracking maps
        int32 total = CountItems();
        for (int32 i = 0; i < total; i++) {
            GuideListRowItem* item = (GuideListRowItem*)ItemAt(i);
            if (item != nullptr) {
                BRect itemFrame = ItemFrame(i);
                item->TrackMouseHover(this, point, itemFrame);
            }
        }
    }

    void MouseDown(BPoint point) override {
        // Handle selection state mapping defaults
        BListView::MouseDown(point);

        BMessage* currentMsg = Window()->CurrentMessage();
        if (currentMsg == nullptr) return;

        int32 buttons = 0;
        int32 clicks = 0;
        
        // --- 1. HANDLE LEFT DOUBLE-CLICKS (WATCH LIVE) ---
        if (currentMsg->FindInt32("clicks", &clicks) == B_OK && clicks == 2) {
            int32 index = IndexOf(point);
            if (index >= 0) {
                GuideListRowItem* item = (GuideListRowItem*)ItemAt(index);
                if (item != nullptr && fParentShortcutTarget != nullptr) {
                    BString cleanNumberOnly = item->fData.channelLabel;
                    int32 sliceIndex = cleanNumberOnly.FindFirst(" ");
                    if (sliceIndex != B_ERROR) {
                        cleanNumberOnly.Truncate(sliceIndex);
                    }
                    cleanNumberOnly.Trim();

                    BMessage playMsg(MSG_PLAY_IN_MEDIAPLAYER);
                    playMsg.AddString("numeric_channel", cleanNumberOnly.String());
                    fParentShortcutTarget->PostMessage(&playMsg);
                }
            }
        } 
        // --- 2. HANDLE RIGHT-CLICKS (CONTEXT MENU) ---
        else if (currentMsg->FindInt32("buttons", &buttons) == B_OK && buttons == B_SECONDARY_MOUSE_BUTTON) {
            int32 index = IndexOf(point);
            if (index >= 0) {
                GuideListRowItem* item = (GuideListRowItem*)ItemAt(index);
                if (item != nullptr) {
                    
                    BString cleanNumberOnly = item->fData.channelLabel;
                    int32 sliceIndex = cleanNumberOnly.FindFirst(" ");
                    if (sliceIndex != B_ERROR) {
                        cleanNumberOnly.Truncate(sliceIndex);
                    }
                    cleanNumberOnly.Trim();

                    BRect itemFrame = ItemFrame(index);
                    
                    // Duplicate your cell placement math to locate the right-clicked program
                    const float kHeaderWidth = 300.0;
                    const float kCellWidth = 350.0;
                    float localX = point.x - itemFrame.left;
                    
                    int32 cellIndex = -1;
                    if (localX > kHeaderWidth) {
                        cellIndex = (int32)((localX - kHeaderWidth) / kCellWidth);
                    }

                    BPopUpMenu* contextMenu = new BPopUpMenu("Context", false, false);
                    
                    // Option A: Watch Live
                    BString watchLabel;
                    watchLabel << "Watch " << cleanNumberOnly.String() << " Live";
                    BMenuItem* playItem = new BMenuItem(watchLabel.String(), NULL);
                    contextMenu->AddItem(playItem);
                    
                    // Options B & C are only valid if we right-clicked on an actual program cell
                    BMenuItem* queueItem = nullptr;
                    BMenuItem* removeItem = nullptr;
                    int32 matchingActiveIndex = -1;

                     if (cellIndex >= 0 && cellIndex < (int32)item->fData.programs.size()) {
                        // Extract target comparison values using your structural variables
                        std::string targetTime = item->fData.programs[cellIndex].timeDisplay.String();
                        std::string targetChannel = cleanNumberOnly.String(); // e.g., "2.1"
                        std::string targetTitle = item->fData.programs[cellIndex].title.String();

                        // =========================================================================
                        // FIXED: PRE-CALCULATE AND CONVERT TO 24-HOUR MILITARY TIME STRINGS
                        // =========================================================================
                        if (targetTime == "LIVE NOW") {
                            time_t now = real_time_clock();
                            struct tm* timeInfo = localtime(&now);
                            int currentHour = timeInfo->tm_hour;
                            int currentMin = timeInfo->tm_min + 1; // Match the +1 min offset
                            
                            if (currentMin >= 60) {
                                currentMin = 0;
                                currentHour = (currentHour + 1) % 24;
                            }
                            
                            char adjustedBuffer[32];
                            sprintf(adjustedBuffer, "%02d:%02d", currentHour, currentMin);
                            targetTime = adjustedBuffer;
                        } else {
                            int hours = 0, minutes = 0;
                            char ampm[16] = {0};
                            
                            if (sscanf(targetTime.c_str(), "%d:%d %15s", &hours, &minutes, ampm) >= 2) {
                                minutes += 1; // Match the +1 min offset
                                if (minutes >= 60) {
                                    minutes = 0;
                                    hours += 1;
                                }

                                // Mimic your window's exact MSG_PREFILL_RECORD_SCHEDULE logicpass
                                std::string ampmStr(ampm);
                                if (ampmStr.find("PM") != std::string::npos || ampmStr.find("pm") != std::string::npos) {
                                    if (hours < 12) hours += 12;
                                } else if (ampmStr.find("AM") != std::string::npos || ampmStr.find("am") != std::string::npos) {
                                    if (hours == 12) hours = 0;
                                }

                                char adjustedBuffer[32];
                                sprintf(adjustedBuffer, "%02d:%02d", hours, minutes);
                                targetTime = adjustedBuffer;
                            }
                        }

                        // Scan active background vector list to check if already queued
                        gScheduleLocker.Lock();
                        int32 activeCounter = 0;
                        for (size_t i = 0; i < gScheduleList.size(); i++) {
                            if (!gScheduleList[i].processed) {
                                bool channelMatch = (gScheduleList[i].channel.find(targetChannel) != std::string::npos);
                                bool timeMatch = (gScheduleList[i].startTime.find(targetTime) != std::string::npos);

                                if (channelMatch && timeMatch) { 
                                    matchingActiveIndex = activeCounter;
                                    break;
                                }
                                activeCounter++;
                            }
                        }
                        gScheduleLocker.Unlock();

                        // Mutually exclusive toggle: Only show one queue management state action
                        if (matchingActiveIndex != -1) {
                            removeItem = new BMenuItem("Remove Queue", NULL);
                            contextMenu->AddItem(removeItem);
                        } else {
                            queueItem = new BMenuItem("Add to Queue", NULL);
                            contextMenu->AddItem(queueItem);
                        }
                    }


                    
                    BPoint screenPoint = ConvertToScreen(point) + BPoint(2, 2);
                    BMenuItem* selectedItem = contextMenu->Go(screenPoint, false, true, false);
                    
                    if (selectedItem != nullptr && fParentShortcutTarget != nullptr) {
                        if (selectedItem == playItem) {
                            BMessage playMsg(MSG_PLAY_IN_MEDIAPLAYER);
                            playMsg.AddString("numeric_channel", cleanNumberOnly.String());
                            fParentShortcutTarget->PostMessage(&playMsg);
                        } 
                        else if (queueItem != nullptr && selectedItem == queueItem) {
                            // 1. Manually prepare the selection package matching your original HandleDoubleClick logic
                            BMessage selectionBroadcast(MSG_PREFILL_RECORD_SCHEDULE);
                            selectionBroadcast.AddString("show_title", item->fData.programs[cellIndex].title.String());
                            selectionBroadcast.AddString("channel_label", item->fData.channelLabel.String());
                            selectionBroadcast.AddInt32("duration_minutes", item->fData.programs[cellIndex].durationMinutes);
                            
                            BString targetSubchannel = item->fData.channelLabel;
                            int32 spaceIndex = targetSubchannel.FindFirst(" ");
                            if (spaceIndex != B_ERROR) {
                                targetSubchannel.Truncate(spaceIndex);
                            }
                            targetSubchannel.Trim();
                            selectionBroadcast.AddString("numeric_subchannel", targetSubchannel.String());

                            // 2. Perform the time shift buffer adjustment pass safely
                            std::string targetTime = item->fData.programs[cellIndex].timeDisplay.String();
                            if (targetTime == "LIVE NOW") {
                                time_t now = real_time_clock();
                                struct tm* timeInfo = localtime(&now);
                                int currentHour = timeInfo->tm_hour;
                                int currentMin = timeInfo->tm_min;
                                
                                currentMin += 1;
                                if (currentMin >= 60) {
                                    currentMin = 0;
                                    currentHour = (currentHour + 1) % 24;
                                }
                                
                                char adjustedBuffer[32];
                                sprintf(adjustedBuffer, "%02d:%02d", currentHour, currentMin);
                                targetTime = adjustedBuffer;
                            } else {
                                int hours = 0, minutes = 0;
                                char ampm[16] = {0};
                                
                                if (sscanf(targetTime.c_str(), "%d:%d %15s", &hours, &minutes, ampm) >= 2) {
                                    minutes += 1;
                                    if (minutes >= 60) {
                                        minutes = 0;
                                        hours += 1;
                                        if (ampm[0] != '\0' && hours > 12) {
                                            hours = 1;
                                        } else if (ampm[0] == '\0' && hours >= 24) {
                                            hours = 0;
                                        }
                                    }
                                    
                                    char adjustedBuffer[32];
                                    if (ampm[0] != '\0') {
                                        sprintf(adjustedBuffer, "%02d:%02d %s", hours, minutes, ampm);
                                    } else {
                                        sprintf(adjustedBuffer, "%02d:%02d", hours, minutes);
                                    }
                                    targetTime = adjustedBuffer;
                                }
                            }

                            // 3. Complete the combined packet payload and send it
                            selectionBroadcast.AddString("start_time", targetTime.c_str());
                            
                            // Flag this specific message package so your handler knows to save it immediately
                            selectionBroadcast.AddBool("auto_commit_queue", true);
                            
                            fParentShortcutTarget->PostMessage(&selectionBroadcast);
                        }

                        else if (removeItem != nullptr && selectedItem == removeItem) {
                            // Dispatch target remove index down to scheduler case
                            BMessage removeMsg(MSG_REMOVE_SCHEDULE);
                            removeMsg.AddInt32("list_index", matchingActiveIndex);
                            fParentShortcutTarget->PostMessage(&removeMsg);
                        }
                    }
                    delete contextMenu;
                }
            }
        }
    }
};


// =========================================================================
// 3. INTERACTIVE TV GUIDE MATRIX WINDOW
// =========================================================================
class RealTVGuideWindow : public BWindow {
public:
    // Added primary parent window pointer mapping link
    RealTVGuideWindow(BRect frame, const std::vector<ChannelGuideItem>& loadedChannels, BListView* mainChannelListView, BWindow* mainAppWindow) 
        : BWindow(frame, "Interactive TV Guide Matrix", B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS) {
        
        ResizeTo(1050, 650);
        
        // Instantiate the top sticky timeline component track
        TimelineHeaderView* headerTimelineBar = new TimelineHeaderView(BRect(0, 0, Bounds().Width(), 40));
        headerTimelineBar->SetExplicitMinSize(BSize(B_SIZE_UNLIMITED, 40));
        headerTimelineBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 40));

        // Use our customized interactive list engine
        fContainerList = new InteractiveGuideListView("guideListContainer", mainAppWindow);
        _BuildGuideRowsFromLiveChannels(loadedChannels, mainChannelListView);
        
        BScrollView* scrollWrapper = new BScrollView("guideScroll", fContainerList, 0, true, true);
        
        // Layer components vertically using the Layout API: Header remains locked on top
        BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
            .SetInsets(0, 0, 0, 0)
            .Add(headerTimelineBar) 
            .Add(scrollWrapper)
        .End();
    }

private:
    BListView* fContainerList;

    void _BuildGuideRowsFromLiveChannels(const std::vector<ChannelGuideItem>& loadedChannels, BListView* mainListView) {
        if (mainListView == nullptr) return;
        
        // Loop through the precise live array records matching your main UI order
        for (size_t i = 0; i < loadedChannels.size(); i++) {
            const auto& liveChan = loadedChannels[i];
            
            // Fetch the corresponding icon directly from your UI cache list
            ChannelListItem* channelItem = (ChannelListItem*)mainListView->ItemAt(i);
            const BBitmap* associatedIcon = (channelItem != nullptr) ? channelItem->channelIcon : nullptr;
            
            GuideRowModel rowData;
            
            // Generate standard label formatting: "5.1 - FOX"
            rowData.channelLabel << liveChan.guideNumber.c_str() << " - " << liveChan.guideName.c_str();
            rowData.channelIcon = associatedIcon;
            
            // Block 1: Ingest Live Show Name Details
            rowData.programs.push_back({liveChan.nowPlaying.c_str(), "LIVE NOW", 350.0f});
            
            // Blocks 2+: Ingest look-ahead items from the futureLineup vector
            // Blocks 2+: Ingest look-ahead items from the futureLineup vector
            for (const auto& nextShow : liveChan.futureLineup) {
                rowData.programs.push_back({
                    nextShow.title.c_str(), 
                    nextShow.startTimeStr.c_str(), 
                    350.0f, 
                    nextShow.durationMinutes // <-- NEW: Passes tracking info to your view components
                });
            }

            // Fallback generation helper if HDHomeRun doesn't return future items for a channel
            if (liveChan.futureLineup.empty()) {
                rowData.programs.push_back({"No Look-Ahead Data Available", "NEXT", 350.0f});
                rowData.programs.push_back({"No Look-Ahead Data Available", "LATER", 350.0f});
            }
            
            fContainerList->AddItem(new GuideListRowItem(rowData, i));
        }
    }
    
};


class DVRWindow : public BWindow {
	private:
	    std::vector<BBitmap*> fIconCache;
		BStringView* fBackendStatusLabel;
		BButton*     fRestartBackendButton;
		BButton* fDateBrowseButton;     
		int32 fCountdownSecondsRemaining; 
		BStringView* fCountdownLabel;     
		std::string fSelectedDirectory;
		BFilePanel* fFolderPanel;      
		BStringView* fPathDisplayLabel; 
		BButton* fBrowseButton;       
		BMenuItem* fNotifyOnItem;
		BMenuItem* fNotifyOffItem;
		BMenuItem* fDebugOnItem;
		BMenuItem* fDebugOffItem;
				
		enum ChannelFilter {
		    FILTER_ALL,
		    FILTER_HD,
		    FILTER_SD
};

ChannelFilter fCurrentFilter;

	BTextControl* fDateInput; 
    BMenuField* fTunerSelector;
    BPopUpMenu* fTunerMenu;
    BTextControl* fChannelInput;
    BMenuField* fDurationSelector; 
    BPopUpMenu* fDurationMenu;
    BTextControl* fTimeInput; 
    BButton* fRecordButton;
    BButton* fStopButton;
    BButton* fScheduleButton; 
    BStringView* fStatusLabel;
    
    BListView* fChannelListView;
    BScrollView* fChannelScrollView;
    std::vector<ChannelGuideItem> fLoadedChannels;
    
    ScheduleListView* fScheduleListView;
    BScrollView* fScheduleScrollView;
    BStringView* fScheduleHeading;

    std::string fSelectedIp;
    std::string fSelectedDurationSeconds; 
    thread_id fActiveThread;
    thread_id fSchedulerThread;
    BMessenger* fSchedulerMessenger;

    std::vector<std::string> DiscoverAllTuners() {
        std::vector<std::string> tuners;
        struct hdhomerun_discover_device_t result_list[64];
        hdhomerun_discover_t* ds = hdhomerun_discover_create(NULL);
        if (ds == NULL) {
            return tuners;
        }
        
        int count = hdhomerun_discover_find_devices_v2(ds, 0, HDHOMERUN_DEVICE_TYPE_TUNER, HDHOMERUN_DEVICE_ID_WILDCARD, result_list, 64);
        for (int i = 0; i < count; i++) {
            uint32_t ip = result_list[i].ip_addr;
            char ip_str[32];            
            sprintf(ip_str, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
            tuners.push_back(std::string(ip_str));
        }
        hdhomerun_discover_destroy(ds);
        return tuners;
    }



    void FetchAndPopulateChannelList() {
        fChannelListView->MakeEmpty();
        fLoadedChannels.clear();

        std::vector<std::string> tuners = DiscoverAllTuners();
        if (tuners.empty()) {
            fStatusLabel->SetText("Status: Guide Error - No active tuners discovered.");
            return;
        }
        std::string targetIp = fSelectedIp.empty() ? tuners[0] : fSelectedIp;

        // ==========================================
        // PARSER 1: THE CLOUD GUIDE DATA FETCH
        // ==========================================
        std::string discoveryUrl = "http://" + targetIp + "/discover.json";
        std::string discoverPayload;
        CURL* curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, discoveryUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &discoverPayload);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }

        std::string deviceAuthToken = "";
        try {
            auto jDisc = json::parse(discoverPayload);
            if (jDisc.is_object() && jDisc.contains("DeviceAuth")) {
                deviceAuthToken = jDisc["DeviceAuth"].get<std::string>();
            }
        } catch (...) {}

        if (deviceAuthToken.empty()) {
            fStatusLabel->SetText("Status: Guide Error - DeviceAuth token not found.");
            return;
        }

        std::string guideUrl = "https://api.hdhomerun.com/api/guide?DeviceAuth=" + deviceAuthToken;
        std::string guidePayload;
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, guideUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &guidePayload);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); 
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }

        // Temporary map to hold our cloud show guide data keyed by Channel Number (e.g., "5.1")
        std::map<std::string, ChannelGuideItem> cloudGuideMap;
        std::map<std::string, std::string> cloudIconMap;

        try {
            auto jGuide = json::parse(guidePayload);
            if (jGuide.is_array()) {
                for (const auto& channelEntry : jGuide) {
                    std::string chNum = channelEntry.value("GuideNumber", "");
                    if (chNum.empty()) continue;

                    ChannelGuideItem item;
                    item.guideNumber = chNum;
                    item.guideName   = channelEntry.value("GuideName", "Unknown");
                    item.nowPlaying  = "To Be Announced";

				    if (channelEntry.contains("Guide") && channelEntry["Guide"].is_array() && !channelEntry["Guide"].empty()) {
				        const auto& guideArray = channelEntry["Guide"];
				        item.nowPlaying = guideArray[0].value("Title", "To Be Announced");

				        std::time_t nowStartEpoch = guideArray[0].value("StartTime", 0);
				        std::time_t nowEndEpoch   = guideArray[0].value("EndTime", 0);
				        
				        if (nowEndEpoch > nowStartEpoch && nowStartEpoch > 0) {
				            item.nowPlayingDurationMinutes = (int32)((nowEndEpoch - nowStartEpoch) / 60);
				        } else {
				            item.nowPlayingDurationMinutes = 30; 
				        }
				        

				        for (size_t g = 1; g < guideArray.size() && g <= 3; g++) {
				            UpcomingShowItem futureShow;
				            futureShow.title = guideArray[g].value("Title", "To Be Announced");
				            
				            std::time_t startEpoch = guideArray[g].value("StartTime", 0);
				            std::time_t endEpoch   = guideArray[g].value("EndTime", 0);
				            
				            if (endEpoch > startEpoch && startEpoch > 0) {
				                futureShow.durationMinutes = (int32)((endEpoch - startEpoch) / 60);
				            } else {
				                futureShow.durationMinutes = 30;
				            }

                            if (startEpoch > 0) {
                                std::tm* sTime = std::localtime(&startEpoch);
                                char timeBuf[32];
                                std::strftime(timeBuf, sizeof(timeBuf), "%I:%M %p", sTime);
                                futureShow.startTimeStr = std::string(timeBuf);
                            } else {
                                futureShow.startTimeStr = "--:--";
                            }
                            item.futureLineup.push_back(futureShow);
                        }

                    }
                    cloudGuideMap[chNum] = item;

                    if (channelEntry.contains("ImageURL")) {
                        cloudIconMap[chNum] = channelEntry["ImageURL"].get<std::string>();
                    }
                }
            }
        } catch (...) {
            fStatusLabel->SetText("Status: Cloud guide parse failed.");
        }

        // ==========================================
        // PARSER 2: THE LOCAL TUNER LINEUP FILTER
        // ==========================================
        std::string lineupUrl = "http://" + targetIp + "/lineup.json";
        std::string lineupPayload;
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, lineupUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &lineupPayload);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L); 
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }

        try {
            auto jLineup = json::parse(lineupPayload);
            if (jLineup.is_array()) {
                for (const auto& channelEntry : jLineup) {
                    std::string chNum = channelEntry.value("GuideNumber", "0.0");
                    
                    int isRealHD = channelEntry.value("HD", 0);
                    if (fCurrentFilter == FILTER_HD && isRealHD == 0) continue;
                    if (fCurrentFilter == FILTER_SD && isRealHD == 1) continue;

                    ChannelGuideItem finalItem;
                    if (cloudGuideMap.find(chNum) != cloudGuideMap.end()) {
                        finalItem = cloudGuideMap[chNum];
                    } else {
                        finalItem.guideNumber = chNum;
                        finalItem.guideName   = channelEntry.value("GuideName", "Unknown");
                        finalItem.nowPlaying  = "Live Stream Available";
                    }

                    fLoadedChannels.push_back(finalItem);

                    // Handle background asset queue management using the cloud image url
                    std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + finalItem.guideName + ".png";
                    std::ifstream checkFile(iconPath.c_str());
                    bool fileExistsOnDisk = checkFile.good();
                    checkFile.close();

                    if (!fileExistsOnDisk && cloudIconMap.find(chNum) != cloudIconMap.end()) {
                        std::string downloadUrl = cloudIconMap[chNum];
                        gIconQueueLocker.Lock();
                        DownloadQueueItem job = { iconPath, downloadUrl, -1 };
                        gIconDownloadQueue.push_back(job);
                        gIconQueueLocker.Unlock();
                    }
                }
            }
        } catch (...) {
            fStatusLabel->SetText("Status: Local lineup filter failed.");
        }

        // ==========================================
        // RENDER STEP: POPULATE INTERFACE WIDGETS
        // ==========================================
        for (size_t i = 0; i < fLoadedChannels.size(); i++) {
            const auto& item = fLoadedChannels[i];

            std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + item.guideName + ".png";
            BBitmap* activeIcon = BTranslationUtils::GetBitmap(iconPath.c_str());
            if (activeIcon != nullptr) {
                if (activeIcon->IsValid()) {
                    fIconCache.push_back(activeIcon);
                } else {
                    delete activeIcon;
                    activeIcon = nullptr;
                }
            }

            std::string displayLabel = item.guideNumber + " - " + item.guideName + " (Now: " + item.nowPlaying + ")";
            fChannelListView->AddItem(new ChannelListItem(displayLabel.c_str(), activeIcon));
        }

        if (gIconWindowMessenger == nullptr) {
            gIconWindowMessenger = new BMessenger(this);
        }

        if (atomic_get(&gIconThreadRunning) == 0) {
            gIconQueueLocker.Lock();
            bool queueHasWork = !gIconDownloadQueue.empty();
            gIconQueueLocker.Unlock();

            if (queueHasWork) {
                thread_id downloader = spawn_thread(SerialIconDownloaderThread, "SerialIconWorker", B_LOW_PRIORITY, NULL);
                if (downloader >= B_OK) resume_thread(downloader);
            }
        }
    }




    void RefreshScheduleListView() {
        fScheduleListView->MakeEmpty();
        gScheduleLocker.Lock();
        for (const auto& item : gScheduleList) {
            if (!item.processed) {
                std::string durText = (item.duration == "1800") ? "30m" : "1h+";
                
                std::string shortDate = (item.startDate.length() >= 10) ? item.startDate.substr(5) : item.startDate;
                
                std::string entryLabel = shortDate + " @ " + item.startTime + " -> Ch " + item.channel + " (" + durText + ")";
                fScheduleListView->AddItem(new BStringItem(entryLabel.c_str()));
            }
        }
        gScheduleLocker.Unlock();
    }


    void AddDurationItem(const char* label, const char* secondsValue, bool isDefault = false) {
        BMessage* msg = new BMessage(MSG_DURATION_SELECTED);
        msg->AddString("seconds", secondsValue);
        BMenuItem* item = new BMenuItem(label, msg);
        if (isDefault) {
            item->SetMarked(true);
            fSelectedDurationSeconds = secondsValue;
        }
        fDurationMenu->AddItem(item);
    }
    
class CalendarWindow : public BWindow {
private:

    std::vector<BBitmap*> fIconCache;
    BPrivate::BCalendarView* fCalendar;
    BMessenger fTargetMessenger;

public:
    CalendarWindow(BPoint spawnPoint, BMessenger target) 
        : BWindow(BRect(spawnPoint.x, spawnPoint.y, spawnPoint.x + 220, spawnPoint.y + 200), 
                  "Select Date", B_MODAL_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE) {
        
        fTargetMessenger = target;

        BView* panel = new BView(Bounds(), "CalPanel", B_FOLLOW_ALL, B_WILL_DRAW);
        panel->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        fCalendar = new BPrivate::BCalendarView(BRect(10, 10, 210, 190), "calendar");
        fCalendar->SetSelectionMessage(new BMessage(MSG_DATE_SELECTED));
        fCalendar->SetTarget(this);

        panel->AddChild(fCalendar);
        AddChild(panel);
    }

    void MessageReceived(BMessage* message) override {
        if (message->what == MSG_DATE_SELECTED) {
            BPrivate::BDate selectedDate = fCalendar->Date();            
            int year  = selectedDate.Year();
            int month = selectedDate.Month();
            int day   = selectedDate.Day();
            char dateBuffer[32];
            sprintf(dateBuffer, "%04d-%02d-%02d", year, month, day);

            BMessage reply(MSG_DATE_SELECTED);
            reply.AddString("date_string", dateBuffer);
            fTargetMessenger.SendMessage(&reply);

            PostMessage(B_QUIT_REQUESTED);
        } else {
            BWindow::MessageReceived(message);
        }
    }
};











// @con
public:
    virtual ~DVRWindow(); 

    DVRWindow() : BWindow(BRect(150, 150, 1030, 625), "Haiku HDHomeRun DVR Scheduler", B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE) {
        LoadSchedulesFromDisk();
        
        thread_id updateThread = spawn_thread(BackgroundUpdateChecker, "UpdateCheckerThread", B_NORMAL_PRIORITY, this);
        if (updateThread >= 0) {
            resume_thread(updateThread);
        }
        
        fSelectedDirectory = gGlobalSaveDirectory;
        fFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL, B_DIRECTORY_NODE, false, new BMessage(MSG_DIR_CHOSEN));
        std::string initialButtonText = "Save To: " + fSelectedDirectory;
        fBrowseButton = new BButton(BRect(20, 300, 330, 335), "browse_btn", initialButtonText.c_str(), new BMessage(MSG_CHOOSE_DIR));                
        fCountdownLabel = new BStringView(BRect(20, 345, 330, 365), "countdown_label", "Time Remaining: --:--");
        fCountdownLabel->SetAlignment(B_ALIGN_CENTER); 
          
     // =========================================================================
        // TOP APPLICATION MENUBAR INITIALIZATION
        // =========================================================================
        BMenuBar* menuBar = new BMenuBar(BRect(0, 0, Bounds().Width(), 20), "top_menubar");

        // 1. Create the Options Dropdown
        BMenu* optionsMenu = new BMenu("Options");
        
        // --- Update Alerts Section ---
        BMessage* msgNotifyOn = new BMessage(MSG_TOGGLE_NOTIFICATIONS);
        msgNotifyOn->AddBool("enable", true);
        fNotifyOnItem = new BMenuItem("Enable Update Alerts", msgNotifyOn);

        BMessage* msgNotifyOff = new BMessage(MSG_TOGGLE_NOTIFICATIONS);
        msgNotifyOff->AddBool("enable", false);
        fNotifyOffItem = new BMenuItem("Disable Update Alerts", msgNotifyOff);

        fNotifyOnItem->SetMarked(cfg.showUpdateNotifications == true);
        fNotifyOffItem->SetMarked(cfg.showUpdateNotifications == false);

        optionsMenu->AddItem(fNotifyOnItem);
        optionsMenu->AddItem(fNotifyOffItem);
        
        // --- ADDED: Debug Mode Section ---
        optionsMenu->AddSeparatorItem(); // Visual separation line

        // Create class-level fields for these items if you want to track them: BMenuItem *fDebugOnItem, *fDebugOffItem;
        BMessage* msgDebugOn = new BMessage(MSG_TOGGLE_DEBUG);
        msgDebugOn->AddBool("enable", true);
        fDebugOnItem = new BMenuItem("Enable Debug Mode", msgDebugOn);

        BMessage* msgDebugOff = new BMessage(MSG_TOGGLE_DEBUG);
        msgDebugOff->AddBool("enable", false);
        fDebugOffItem = new BMenuItem("Disable Debug Mode", msgDebugOff);

        fDebugOnItem->SetMarked(cfg.debugEnable == true);
        fDebugOffItem->SetMarked(cfg.debugEnable == false);

        optionsMenu->AddItem(fDebugOnItem);
        optionsMenu->AddItem(fDebugOffItem);
        
        // --- NEW CONTENT: Guide look-Ahead Window Trigger ---
        optionsMenu->AddSeparatorItem(); 
        
        // Adds the menu option with 'G' mapped as the Alt+G keyboard shortcut
        BMessage* msgOpenGuide = new BMessage(MSG_OPEN_GUIDE);
        BMenuItem* guideItem = new BMenuItem("Open Look-Ahead Guide...", msgOpenGuide, 'G');
        optionsMenu->AddItem(guideItem);
        
        optionsMenu->AddSeparatorItem();
		BMenuItem* firmwareItem = new BMenuItem("Check Tuner Firmware...", new BMessage(MSG_CHECK_FIRMWARE));
		optionsMenu->AddItem(firmwareItem);
        
        // Add Options first so it sits on the far left side
        menuBar->AddItem(optionsMenu);        
      

        // 2. Create your original Channel Filter Dropdown
        fCurrentFilter = FILTER_ALL;
        BMenu* filterMenu = new BMenu("Filter");
        BMenuItem* itemAll = new BMenuItem("All Channels", new BMessage(MSG_FILTER_ALL));
        BMenuItem* itemHd  = new BMenuItem("HD Only",      new BMessage(MSG_FILTER_HD));
        BMenuItem* itemSd  = new BMenuItem("SD Only",      new BMessage(MSG_FILTER_SD));
        itemAll->SetMarked(true); 
        filterMenu->AddItem(itemAll);
        filterMenu->AddItem(itemHd);
        filterMenu->AddItem(itemSd);
        
        // Add Filter second so it sits directly to the right of Options
        menuBar->AddItem(filterMenu);        
        AddChild(menuBar);

        
        
		BView* view = new BView(Bounds(), "MainView", B_FOLLOW_ALL, B_WILL_DRAW);
		
        view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));        
        fTunerMenu = new BPopUpMenu("Select Tuner");
        std::vector<std::string> foundTuners = DiscoverAllTuners();
        fSelectedIp = foundTuners[0];        
        for (size_t i = 0; i < foundTuners.size(); i++) {
            BMessage* msg = new BMessage(MSG_TUNER_SELECTED);
            msg->AddString("ip", foundTuners[i].c_str());
            BMenuItem* item = new BMenuItem(foundTuners[i].c_str(), msg);
            if (foundTuners[i] == fSelectedIp) item->SetMarked(true);
            fTunerMenu->AddItem(item);
        }        
        fDurationMenu = new BPopUpMenu("Select Duration");
        AddDurationItem("30 Minutes", "1800", true);  
        AddDurationItem("1 Hour",     "3600");
        AddDurationItem("1.5 Hours",  "5400");
        AddDurationItem("2 Hours",    "7200");        
        fTunerSelector = new BMenuField(BRect(20, 35, 330, 60), "tuner_field", "Tuner IP:", fTunerMenu);
        fTunerSelector->SetDivider(85.0);
        fChannelInput = new BTextControl(BRect(20, 70, 330, 95), "channel", "Channel:", "5.1", NULL);
        fChannelInput->SetDivider(85.0);
         fChannelInput->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
        fDurationSelector = new BMenuField(BRect(20, 105, 330, 130), "duration_field", "Duration:", fDurationMenu);
        fDurationSelector->SetDivider(85.0); 
        BFont digitalFont(be_fixed_font);
        digitalFont.SetSize(13.0);
        rgb_color digitalGreen = { 0, 130, 0, 255 };
        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        char dateBuffer[32];
        std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", localTime);
        fDateInput = new BTextControl(BRect(20, 140, 260, 165), "date", "Start Date:", dateBuffer, NULL);
        fDateInput->SetDivider(85.0);
         fDateInput->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
        fDateInput->TextView()->MakeEditable(false);
        fDateInput->TextView()->SetStylable(false);        
        fDateInput->TextView()->SetFontAndColor(&digitalFont, B_FONT_ALL, &digitalGreen);
        fDateInput->TextView()->SetAlignment(B_ALIGN_CENTER);
        fDateBrowseButton = new BButton(BRect(270, 140, 330, 165), "date_browse", "Date", new BMessage(MSG_POPUP_CALENDAR));
        // 1. Query the live system clock profile
        std::time_t rawCurrentTime = std::time(nullptr);
        std::tm* localTimeStruct = std::localtime(&rawCurrentTime);

        // 2. Format the time components cleanly into a standard "HH:MM" text string
        char timeTextBuffer[16];
        // Note: %H maps 24-hour style (00-23), use %I for 12-hour formatting if your schedule matches AM/PM
        std::strftime(timeTextBuffer, sizeof(timeTextBuffer), "%H:%M", localTimeStruct);

        // 3. Inject the processed runtime buffer straight into the initial input string value field
        fTimeInput = new BTextControl(BRect(20, 175, 260, 200), "time", "Start Time:", timeTextBuffer, NULL);
        fTimeInput->SetDivider(85.0);

         fTimeInput->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));    
        fTimeInput->TextView()->SetFontAndColor(&digitalFont, B_FONT_ALL, &digitalGreen);
        fTimeInput->TextView()->SetAlignment(B_ALIGN_CENTER);
        BButton* btnTimeUp = new BButton(BRect(270, 175, 295, 198), "time_up", "+", new BMessage(MSG_CLOCK_UP));
        BButton* btnTimeDown = new BButton(BRect(305, 175, 330, 198), "time_down", "-", new BMessage(MSG_CLOCK_DOWN));
        fRecordButton = new BButton(BRect(20, 215, 170, 250), "record", "Start Recording", new BMessage(MSG_START_RECORDING));
        fStopButton = new BButton(BRect(180, 215, 330, 250), "stop", "Stop Recording", new BMessage(MSG_STOP_RECORDING));
        fStopButton->SetEnabled(false);        
        fScheduleButton = new BButton(BRect(20, 255, 330, 290), "schedule", "Queue Scheduled Show", new BMessage(MSG_ADD_SCHEDULE));        
        fStatusLabel = new BStringView(BRect(20, 440, 830, 465), "status", "Status: Idle");
        fStatusLabel->SetAlignment(B_ALIGN_LEFT);                       
        fChannelListView = new BListView(BRect(0, 0, 480, 180), "channel_list", B_SINGLE_SELECTION_LIST);
        fChannelListView->SetSelectionMessage(new BMessage(MSG_CHANNEL_CLICKED));        
        fChannelScrollView = new BScrollView("scroll_channels", fChannelListView, B_FOLLOW_LEFT | B_FOLLOW_TOP, 0, false, true);
        fScheduleHeading = new BStringView(BRect(360, 230, 860, 250), "sch_head", "Active Queued Schedules (Right Click to Delete):");        
        fScheduleListView = new ScheduleListView(BRect(0, 0, 480, 105), "schedule_list");
        fScheduleScrollView = new BScrollView("scroll_schedules", fScheduleListView, B_FOLLOW_LEFT | B_FOLLOW_TOP, 0, false, true);       
        fChannelScrollView->MoveTo(360, 35);
        fChannelScrollView->ResizeTo(500, 180);        
        fScheduleHeading->MoveTo(360, 230);        
        fScheduleScrollView->MoveTo(360, 255);
        fScheduleScrollView->ResizeTo(500, 105);
        
        fBackendStatusLabel = new BStringView(BRect(5, 5, 125, 25), "backend_status", "Backend: Checking...");
        fBackendStatusLabel->SetFont(be_bold_font);
        
        // --- Right Column: Interactive Sidebars & Status Panel ---
        fRestartBackendButton = new BButton(BRect(730, 365, 860, 395), "restart_backend", "Abort!", new BMessage(MSG_RESTART_BACKEND));
        fRestartBackendButton->SetToolTip("Warning: Abort! will immediately abort any active scheduled recording streams currently in progress!");
        BBox* statusBox = new BBox(BRect(730, 405, 860, 435), "bebox_status_wrapper");
        statusBox->SetBorder(B_FANCY_BORDER); 

        fBackendStatusLabel = new BStringView(BRect(5, 5, 125, 25), "backend_status", "Backend: Checking...");
        
        BFont monoFont(be_fixed_font);
        monoFont.SetSize(11.0);
        fBackendStatusLabel->SetFont(&monoFont);
        fBackendStatusLabel->SetAlignment(B_ALIGN_CENTER);

        // Mount the text label inside the bounding container frame box
        statusBox->AddChild(fBackendStatusLabel);

        // --- Left Column: Configuration Forms ---
        view->AddChild(fTunerSelector);
        view->AddChild(fChannelInput);
        view->AddChild(fDurationSelector);
        view->AddChild(fDateInput);         
        view->AddChild(fDateBrowseButton);   
        view->AddChild(fTimeInput);
        view->AddChild(btnTimeUp);
        view->AddChild(btnTimeDown);

        // --- Left Column: Action Triggers & Status ---
        view->AddChild(fRecordButton);
        view->AddChild(fStopButton);
        view->AddChild(fScheduleButton);
        view->AddChild(fBrowseButton);
        view->AddChild(fStatusLabel);
        view->AddChild(fCountdownLabel);

        // --- Right Column: Interactive Sidebars & Status Panel ---
        view->AddChild(fChannelScrollView);
        view->AddChild(fScheduleHeading);
        view->AddChild(fScheduleScrollView);
        view->AddChild(fRestartBackendButton); 
        view->AddChild(statusBox);             

        AddChild(view);

        fActiveThread = -1;        
        fSchedulerMessenger = new BMessenger(this);
        fSchedulerThread = spawn_thread(ClockSchedulerThread, "DVRCronEngine", B_NORMAL_PRIORITY, fSchedulerMessenger);
        if (fSchedulerThread >= B_OK) {
            resume_thread(fSchedulerThread);
        }               

        FetchAndPopulateChannelList();
        RefreshScheduleListView();   
    }

    bool QuitRequested() override {
        atomic_set(&gStopScheduler, 1);
        atomic_set(&gCancelRecording, 1);
        return true;
    }

	void WindowActivated(bool active) {
	    BWindow::WindowActivated(active);
	    PostMessage(B_COLORS_UPDATED);
	}    
	
		
	
    void MessageReceived(BMessage* message) override {
        switch (message->what) {
        	
        	
        case MSG_TOGGLE_DEBUG: {
            bool enableDebug = true;
            if (message->FindBool("enable", &enableDebug) == B_OK) {
                // Update global cfg object field directly
                cfg.debugEnable = enableDebug;
                
                // Toggle radio-style UI checkmarks
                fDebugOnItem->SetMarked(cfg.debugEnable == true);
                fDebugOffItem->SetMarked(cfg.debugEnable == false);
                
                // Instantly commit preference state change directly into disk storage
                SaveSchedulesToDisk();
                
                // This will always fire regardless of configuration state because we just toggled it
                printf("[DEBUG_SYS] System logging runtime state mutated via UI: %s\n", 
                       cfg.debugEnable ? "ENABLED" : "DISABLED");
            }
            break;
        }
       	
        case MSG_TOGGLE_NOTIFICATIONS: {
            bool enableAlerts = true;
            if (message->FindBool("enable", &enableAlerts) == B_OK) {
                // Update global cfg object field
                cfg.showUpdateNotifications = enableAlerts;
                
                // Toggle the UI checkbox checkmarks
                fNotifyOnItem->SetMarked(cfg.showUpdateNotifications == true);
                fNotifyOffItem->SetMarked(cfg.showUpdateNotifications == false);
                
                // Instantly commit preference state change directly into disk storage
                SaveSchedulesToDisk();
                
                if (cfg.debugEnable) {
                    printf("[DEBUG_UPDATE] Notification configuration mutated via UI: %s\n", 
                           cfg.showUpdateNotifications ? "ENABLED" : "DISABLED");
                }
            }
            break;
        }

            
        case MSG_POLL_BACKEND: {
            LoadSchedulesFromDisk();
            RefreshScheduleListView();
            BMessenger serviceTarget("application/x-vnd.haikuhdhomerun-dvr");
            bool isRunning = serviceTarget.IsValid();            
            rgb_color activeGreen = { 0, 160, 0, 255 };  
            rgb_color alertRed    = { 225, 0, 0, 255 };  

            if (isRunning) {
                fBackendStatusLabel->SetText("CONNECTED");
                fBackendStatusLabel->SetHighColor(activeGreen); 
            } else {
                fBackendStatusLabel->SetText("OFFLINE");
                fBackendStatusLabel->SetHighColor(alertRed); 
            }
            
            fBackendStatusLabel->Invalidate();
            break;
        }


   	
	     case MSG_POPUP_CALENDAR: {
	         BPoint spawnPoint = ConvertToScreen(fDateInput->Frame().LeftBottom());
	         spawnPoint.y += 5; // Add a tiny 5-pixel padding spacing gap
	         
	         CalendarWindow* calWin = new CalendarWindow(spawnPoint, BMessenger(this));
	         calWin->Show();
	         break;
	     }
	
	     case MSG_DATE_SELECTED: {
	         const char* newDateString = nullptr;
	         if (message->FindString("date_string", &newDateString) == B_OK) {
	             fDateInput->SetText(newDateString);
	         }
	         break;
	     }
	
	
	
	     case MSG_DISK_SPACE_WARNING: {
	         int32 freeMB = 0;
	         if (message->FindInt32("free_mb", &freeMB) == B_OK) {
	             char warningMessage[128];
	             sprintf(warningMessage, "CRITICAL WARNING: Storage space running low! Only %" B_PRId32 " MB remaining in save directory.", freeMB);
	             
	             // Push the text to your full-width bottom dashboard row
	             fStatusLabel->SetText(warningMessage);
	             
	             // Update text color to high-visibility alarm red
	             rgb_color alertRed = { 225, 0, 0, 255 };
	             fStatusLabel->SetHighColor(alertRed);
	             fStatusLabel->Invalidate();
	         }
	         break;
	     }

         case MSG_STREAM_PROGRESS_UPDATE: {
             double mbDownloaded = 0.0;
             if (message->FindDouble("bytes_now", &mbDownloaded) == B_OK) {
                 char progressString[256];
                 sprintf(progressString, "Status: Active Capture Stream Operational ... [ Total Data Written: %.2f MB ]", mbDownloaded);
                 
                 fStatusLabel->SetText(progressString);
                 fStatusLabel->SetFont(be_bold_font);
                 
                 // FIX: Replaced green with your system default text color token
                 fStatusLabel->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
                 fStatusLabel->Invalidate();
             }
             break;
         }



	     case MSG_CLOCK_UP:
	     case MSG_CLOCK_DOWN: {
	         std::string timeStr = fTimeInput->Text();
	         size_t colonPos = timeStr.find(':');
	         if (colonPos != std::string::npos) {
	             int hours = std::atoi(timeStr.substr(0, colonPos).c_str());
	             int minutes = std::atoi(timeStr.substr(colonPos + 1).c_str());
	             
	             if (message->what == MSG_CLOCK_UP) {
	                 minutes += 15;
	             } else {
	                 minutes -= 15;
	             }
	             
	             if (minutes >= 60) { minutes = 0; hours++; }
	             if (minutes < 0) { minutes = 45; hours--; }
	             if (hours >= 24) { hours = 0; }
	             if (hours < 0) { hours = 23; }
	             
	
	             char updatedTimeBuffer[16];
	             sprintf(updatedTimeBuffer, "%02d:%02d", hours, minutes);
	             fTimeInput->SetText(updatedTimeBuffer);
	
	         }
	         break;
	     }


		  case B_COLORS_UPDATED: {
		      rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);	      
		      int brightness = ((panelBg.red * 299) + (panelBg.green * 587) + (panelBg.blue * 114)) / 1000;
		      
		      rgb_color textColor;
		      if (brightness < 125) {
		          textColor = (rgb_color){ 255, 255, 255, 255 }; 
		      } else {
		          textColor = (rgb_color){ 0, 0, 0, 255 };     
		      }
		
		      BView* mainView = FindView("MainView");
		      if (mainView != nullptr) {
		          mainView->SetViewColor(panelBg);
		          mainView->SetLowColor(panelBg);
		
		          for (int32 i = 0; i < mainView->CountChildren(); i++) {
		              BView* child = mainView->ChildAt(i);	              
		              if (dynamic_cast<BStringView*>(child) != nullptr) {
		                  if (child != fBackendStatusLabel) {
		                      child->SetHighColor(textColor);
		                  }
		              }	              
		              child->SetLowColor(panelBg);
		              child->Invalidate();
		          }
		      }	
		      BWindow::MessageReceived(message);
		      break;
		  }
	
	     case MSG_CHOOSE_DIR:
	         if (fFolderPanel) fFolderPanel->Show();
	         break;
	         
		 case MSG_DIR_CHOSEN: {
		      entry_ref ref;
		      if (message->FindRef("refs", &ref) == B_OK) {
		          BEntry entry(&ref, true);
		          BPath path;
		          
		          if (entry.GetPath(&path) == B_OK) {
		              fSelectedDirectory = path.Path();
		              gGlobalSaveDirectory = fSelectedDirectory;	
		              std::string newButtonLabel = "Save To: " + fSelectedDirectory;
		              fBrowseButton->SetLabel(newButtonLabel.c_str());	
		              SaveSchedulesToDisk();
		          }
		      }
		      break;
		  }

 	
         case MSG_FILTER_ALL:
         case MSG_FILTER_HD:
         case MSG_FILTER_SD: {
             BMenuBar* menuBar = dynamic_cast<BMenuBar*>(FindView("top_menubar"));
             if (menuBar) {
                 // CHANGED FROM 0 TO 1: FilterMenu is now the second item in the bar
                 BMenu* filterMenu = menuBar->SubmenuAt(1);
                 if (filterMenu) {
                     for (int32 i = 0; i < filterMenu->CountItems(); i++) {
                         filterMenu->ItemAt(i)->SetMarked(false);
                     }
                 }
             }

             BMenuItem* clickedItem = nullptr;
             message->FindPointer("source", (void**)&clickedItem);
             if (clickedItem) clickedItem->SetMarked(true);

             if (message->what == MSG_FILTER_ALL) fCurrentFilter = FILTER_ALL;
             else if (message->what == MSG_FILTER_HD) fCurrentFilter = FILTER_HD;
             else if (message->what == MSG_FILTER_SD) fCurrentFilter = FILTER_SD;

             FetchAndPopulateChannelList();
             break;
         }


	     case MSG_REFRESH_CHANNEL_LIST_ICONS: {
	         int32 targetRow = -1;
	         if (message->FindInt32("row_index", &targetRow) == B_OK) {
	             if (targetRow >= 0 && targetRow < fChannelListView->CountItems()) {
	                 
	                 const auto& item = fLoadedChannels[targetRow];
	                 std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + item.guideName + ".png";
	                 
	                 BBitmap* freshIcon = BTranslationUtils::GetBitmap(iconPath.c_str());
	                 if (freshIcon != nullptr) {
	                     if (freshIcon->IsValid()) {
	                         fIconCache.push_back(freshIcon);
	                         
	                         ChannelListItem* rowWidget = static_cast<ChannelListItem*>(fChannelListView->ItemAt(targetRow));
	                         if (rowWidget != nullptr) {
	                             rowWidget->channelIcon = freshIcon; 
	                             fChannelListView->InvalidateItem(targetRow); 
	                         }
	                     } else {
	                         delete freshIcon;
	                     }
	                 }
	             }
	         }
	         break;
	     }

	     case MSG_REMOVE_SCHEDULE: {
	         int32 listIndex = -1;
	         if (message->FindInt32("list_index", &listIndex) == B_OK && listIndex >= 0) {
	             gScheduleLocker.Lock();
	             int32 activeCounter = 0;
	             for (size_t i = 0; i < gScheduleList.size(); i++) {
	                 if (!gScheduleList[i].processed) {
	                     if (activeCounter == listIndex) {
	                         gScheduleList.erase(gScheduleList.begin() + i);
	                         break;
	                     }
	                     activeCounter++;
	                 }
	             }
	             gScheduleLocker.Unlock();
	             
	             SaveSchedulesToDisk();     
	             RefreshScheduleListView(); 
	             
	          	 if (fChannelListView != nullptr) {
             		fChannelListView->Invalidate(); 
       			  }            
	             
	             fStatusLabel->SetText("Status: Schedule deleted.");
	         }
	         break;
	     }
     
	     case MSG_REFRESH_SCHEDULES:
	         RefreshScheduleListView();
	         break;


	     case MSG_CHANNEL_CLICKED: {
	         int32 selection = fChannelListView->CurrentSelection();
	         if (selection >= 0 && (size_t)selection < fLoadedChannels.size()) {
	             const auto& channel = fLoadedChannels[selection];
	             
	             fChannelInput->SetText(channel.guideNumber.c_str());
	             
	             std::string guidePreview = "Lineup for " + channel.guideName + ": ";
	             if (!channel.futureLineup.empty()) {
	                 for (size_t s = 0; s < channel.futureLineup.size(); s++) {
	                     guidePreview += "[" + channel.futureLineup[s].startTimeStr + "] " 
	                                  + channel.futureLineup[s].title;
	                     if (s < channel.futureLineup.size() - 1) guidePreview += "  |  ";
	                 }
	             } else {
	                 guidePreview += "No upcoming schedule data available.";
	             }
	             
	             fStatusLabel->SetText(guidePreview.c_str());
	             fStatusLabel->SetFont(be_bold_font);
	             
	             // Keep the text color synchronized with your system's interface theme settings
	             fStatusLabel->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	             fStatusLabel->Invalidate();
	         }
	         break;
	     }

		case MSG_RESTART_BACKEND:
		{
		    BRoster roster;
		    const char* backendSignature = "x-vnd.haikuhdhomerun-dvr";
		
		    if (roster.IsRunning(backendSignature)) {
		        BMessenger backendMessenger(backendSignature);
		        if (backendMessenger.IsValid()) {
		            BMessage quitMessage(B_QUIT_REQUESTED);
		            backendMessenger.SendMessage(&quitMessage);
		        }
		    } else {

		        pid_t pid = fork();
		        if (pid == 0) {
		            close(STDIN_FILENO); 
		            char* const args[] = { 
		                (char*)"/bin/launch_roster", 
		                (char*)"restart", 
		                (char*)backendSignature, 
		                nullptr 
		            };
		            execv(args[0], args);
		            _exit(1); 
		        }
		    }
		    break;
		}

        case MSG_OPEN_GUIDE: {
            BRect guideFrame(100, 100, 1050, 700);
			RealTVGuideWindow* guideWin = new RealTVGuideWindow(guideFrame, fLoadedChannels, fChannelListView, this);
            guideWin->Show();
            break;
        }
/*
         case MSG_PLAY_IN_MEDIAPLAYER: {
            BString numericChannel;
            if (message->FindString("numeric_channel", &numericChannel) == B_OK) {
                
                BString currentIp(fSelectedIp.c_str());
                if (currentIp.IsEmpty()) {
                    currentIp = "127.0.0.1";
                }
                
                // Formulate the exact functional media port stream address URL
                BString streamUrl;
                streamUrl.SetToFormat("http://%s:5004/auto/v%s", currentIp.String(), numericChannel.String());
                
                if (cfg.debugEnable) {
                    printf("[DEBUG PLAYER] Passing command-line args to Media Player: %s\n", streamUrl.String());
                }

                // =========================================================================
                // FIXED: Construct a standard system command-line argument vector packet
                // =========================================================================
                BMessage launchMessage(B_ARGV_RECEIVED); // <-- Forces Haiku to interpret this as a terminal execution line!
                
                // Index 0 must contain the application path, Index 1 is our video stream url
                launchMessage.AddString("argv", "/boot/system/apps/MediaPlayer");

                launchMessage.AddString("argv", streamUrl.String());
                
                // Pack the absolute item count (argc = 2 elements total)
                launchMessage.AddInt32("argc", 2);

                // Fire the system roster to launch the app thread asynchronously
                status_t launchResult = be_roster->Launch("application/x-vnd.Haiku-MediaPlayer", &launchMessage);
                
                if (launchResult != B_OK) {
                    BString errorStatus = "Playback Error: Could not launch MediaPlayer. Status: ";
                    errorStatus << launchResult;
                    fStatusLabel->SetText(errorStatus.String());
                } else {
                    BString playingNotification = "Streaming Live Channel: ";
                    playingNotification << numericChannel;
                    fStatusLabel->SetText(playingNotification.String());
                }
            }
            break;
        }
*/
        case MSG_PLAY_IN_MEDIAPLAYER: {
            BString numericChannel;
            if (message->FindString("numeric_channel", &numericChannel) == B_OK) {
                
                BString currentIp(fSelectedIp.c_str());
                if (currentIp.IsEmpty()) {
                    currentIp = "127.0.0.1";
                }
                
                BString streamUrl;
                streamUrl.SetToFormat("http://%s:5004/auto/v%s", currentIp.String(), numericChannel.String());
                
                if (cfg.debugEnable) {
                    printf("[DEBUG PLAYER] Forking independent system terminal process for mpv: %s\n", streamUrl.String());
                }

                // =========================================================================
                // FIXED: FORK AN INDEPENDENT SYSTEM THREAD PROCESS DIRECTLY
                // =========================================================================
                pid_t processId = fork();
                
                if (processId < 0) {
                    // Fork engine allocation failed entirely
                    fStatusLabel->SetText("Playback Error: Failed to fork execution thread.");
                } 
                else if (processId == 0) {
                    // --- CHILD PROCESS THREAD INTERFACE ---
                    // Explicitly define a real character pointer vector block array
                    char* mpvArgs[3];
                    mpvArgs[0] = (char*)"/boot/system/bin/mpv";
                    mpvArgs[1] = (char*)streamUrl.String();
                    mpvArgs[2] = nullptr; // Array must be strictly null-terminated
                    
                    // Directly swap the active child process context directly onto mpv binary code lines
                    execv(mpvArgs[0], mpvArgs);
                    
                    // If execv fails or errors out, force the rogue background loop to exit instantly
                    _exit(1);
                } 
                else {
                    // --- PARENT APPLICATION CONTROL INTERFACE ---
                    // The main application continues running smoothly at 100% speed
                    BString playingNotification = "Streaming Live via mpv: Channel ";
                    playingNotification << numericChannel;
                    fStatusLabel->SetText(playingNotification.String());
                }
            }
            break;
        }


        case MSG_PREFILL_RECORD_SCHEDULE: {
            BString showTitle, startTime, channelLabel, numericSubchannel;
            int32 durationMinutes = 0;
            
            if (message->FindString("show_title", &showTitle) == B_OK &&
                message->FindString("start_time", &startTime) == B_OK &&
                message->FindString("channel_label", &channelLabel) == B_OK &&
                message->FindString("numeric_subchannel", &numericSubchannel) == B_OK &&
                message->FindInt32("duration_minutes", &durationMinutes) == B_OK) {
                
                // 1. Time string pre-fill conversion parser block
                if (fTimeInput != nullptr) {
                    BString processedTime = startTime;
                    if (startTime.IFindFirst("PM") != B_ERROR) {
                        int32 hour = 0;
                        int32 minute = 0;
                        // FIXED: Parse out both hour AND minutes to prevent rounding down to the top of the hour!
                        if (sscanf(startTime.String(), "%d:%d", &hour, &minute) >= 1) {
                            if (hour < 12) hour += 12;
                            processedTime.SetToFormat("%02d:%02d", hour, minute);
                        }
                    } else if (startTime.IFindFirst("AM") != B_ERROR) {
                        int32 hour = 0;
                        int32 minute = 0;
                        // FIXED: Parse out both hour AND minutes
                        if (sscanf(startTime.String(), "%d:%d", &hour, &minute) >= 1) {
                            if (hour == 12) hour = 0;
                            processedTime.SetToFormat("%02d:%02d", hour, minute);
                        }
                    } else if (startTime == "LIVE NOW") {
                        std::time_t raw = std::time(nullptr);
                        std::tm* loc = std::localtime(&raw);
                        char tBuf[32];
                        std::strftime(tBuf, sizeof(tBuf), "%H:%M", loc);
                        processedTime = tBuf;
                    }
                    fTimeInput->SetText(processedTime.String());
                }

                // 2. PopUpMenu duration selection logic
                if (fDurationMenu != nullptr && durationMinutes > 0) {
                    BString targetDurationLabel;
                    targetDurationLabel << durationMinutes << " Mins";
                    BMenuItem* matchingItem = fDurationMenu->FindItem(targetDurationLabel.String());
                    
                    if (matchingItem != nullptr) {
                        matchingItem->SetMarked(true);
                    } else {
                        BMessage* customDurationMsg = new BMessage(MSG_DURATION_SELECTED); 
                        customDurationMsg->AddInt32("minutes", durationMinutes);
                        BMenuItem* dynamicItem = new BMenuItem(targetDurationLabel.String(), customDurationMsg);
                        fDurationMenu->AddItem(dynamicItem);
                        dynamicItem->SetMarked(true);
                    }
                    
                    BMenuField* parentField = dynamic_cast<BMenuField*>(fDurationMenu->Supermenu());
                    if (parentField != nullptr && parentField->MenuItem() != nullptr) {
                        parentField->MenuItem()->SetLabel(targetDurationLabel.String());
                    }
                }
                
                // =========================================================================
                // 3. FIXED: AUTO-CLEAN AND PRE-FILL TEXT BOX CHANNELS
                // =========================================================================
                if (!numericSubchannel.IsEmpty()) {
                    BString cleanNumberOnly = numericSubchannel;
                    int32 sliceIndex = cleanNumberOnly.FindFirst(" ");
                    if (sliceIndex != B_ERROR) {
                        cleanNumberOnly.Truncate(sliceIndex);
                    }
                    int32 hyphenIndex = cleanNumberOnly.FindFirst("-");
                    if (hyphenIndex != B_ERROR) {
                        cleanNumberOnly.Truncate(hyphenIndex);
                    }
                    cleanNumberOnly.Trim();

                    if (fChannelInput != nullptr) {
                        fChannelInput->SetText(cleanNumberOnly.String());
                    }

                    if (fChannelListView != nullptr) {
                        int32 totalItems = fChannelListView->CountItems();
                        for (int32 i = 0; i < totalItems; i++) {
                            ChannelListItem* listItem = (ChannelListItem*)fChannelListView->ItemAt(i);
                            if (listItem != nullptr) {
                                BString listText(listItem->textDisplay.c_str());
                                if (listText.StartsWith(cleanNumberOnly.String())) {
                                    fChannelListView->Select(i); 
                                    fChannelListView->ScrollToSelection(); 
                                    break;
                                }
                            }
                        }
                    }
                }
                
                // Maintain your status banner notice updates
                BString trackingNotice = "Selected Lineup: ";
                trackingNotice << showTitle;
                fStatusLabel->SetText(trackingNotice.String());

                // =========================================================================
                // 4. INTEGRATED AUTO-COMMIT QUEUE INJECTION
                // =========================================================================
                bool autoCommit = false;
                if (message->FindBool("auto_commit_queue", &autoCommit) == B_OK && autoCommit) {
                    std::string rawTime = fTimeInput->Text();
                    
                    // Run a final check on formatting layout rules
                    if (rawTime.length() == 4 && rawTime.find(':') == std::string::npos) {
                        rawTime.insert(2, ":");
                        fTimeInput->SetText(rawTime.c_str());
                    }
                    
                    ScheduleItem item;
                    item.startDate = fDateInput->Text(); 
                    item.startTime = rawTime; 
                    item.channel = fChannelInput->Text();
                    
                    // FIXED: Convert the integer duration minutes directly to seconds 
                    // to avoid doing math on the fSelectedDurationSeconds std::string object.
                    item.duration = (uint32)durationMinutes * 60;
                    item.processed = false;
                    
                    BMenuItem* markedTuner = fTunerMenu->FindMarked();
                    if (markedTuner != nullptr) {
                        item.tunerIp = markedTuner->Label();
                    } else {
                        item.tunerIp = fSelectedIp;
                    }
                    
                    gScheduleLocker.Lock();
                    gScheduleList.push_back(item);
                    gScheduleLocker.Unlock();
                    
                    SaveSchedulesToDisk(); 
                    RefreshScheduleListView(); 
			        if (fChannelListView != nullptr) {
			             fChannelListView->Invalidate(); 
			        }
                    std::string confMsg = "Queued for " + item.startTime + " (Ch " + item.channel + ")";
                    fStatusLabel->SetText(confMsg.c_str());
                }

            }
            break;
        }




     case MSG_DURATION_SELECTED: {
         const char* secs;
         if (message->FindString("seconds", &secs) == B_OK) {
             fSelectedDurationSeconds = secs;
         }
         break;
     }
     
     case MSG_ADD_SCHEDULE: {
     	std::string rawTime = fTimeInput->Text();
         
         // Quick validation: Ensure it's in HH:MM format
         if (rawTime.length() == 4 && rawTime.find(':') == std::string::npos) {
             // If they typed '0548', turn it into '05:48'
             rawTime.insert(2, ":");
             fTimeInput->SetText(rawTime.c_str());
         }
     	
         ScheduleItem item;
         item.startDate = fDateInput->Text(); 
         item.startTime = fTimeInput->Text();
         item.channel = fChannelInput->Text();
         item.duration = fSelectedDurationSeconds; 
         item.processed = false;
         
         BMenuItem* markedTuner = fTunerMenu->FindMarked();
         if (markedTuner != nullptr) {
             item.tunerIp = markedTuner->Label();
         } else {
             item.tunerIp = fSelectedIp;
         }
         
         gScheduleLocker.Lock();
         gScheduleList.push_back(item);
         gScheduleLocker.Unlock();
         
         SaveSchedulesToDisk(); 
         RefreshScheduleListView(); 

         std::string confMsg = "Queued for " + item.startTime + " (Ch " + item.channel + ")";
         fStatusLabel->SetText(confMsg.c_str());
         break;
     }


     case MSG_TUNER_SELECTED: {
         const char* newIp;
         if (message->FindString("ip", &newIp) == B_OK) {
             fSelectedIp = newIp;
             std::string statusMsg = "Selected Tuner: " + fSelectedIp;
             fStatusLabel->SetText(statusMsg.c_str());
             FetchAndPopulateChannelList();
         }
         break;
     }
     case MSG_START_RECORDING: {
     	
         const char* targetChannel = fChannelInput->Text();
         std::string targetDuration = fSelectedDurationSeconds;

         const char* forcedChannel = nullptr;
         const char* forcedDuration = nullptr;
         if (message->FindString("forced_channel", &forcedChannel) == B_OK) targetChannel = forcedChannel;
         if (message->FindString("forced_duration", &forcedDuration) == B_OK) targetDuration = forcedDuration;
         
         bool tunerAcquired = false;
         RecordingConfig* config = new RecordingConfig();
         config->windowMessenger = BMessenger(this);
         config->channel = targetChannel;
         config->duration = targetDuration;

         std::vector<std::string> foundTuners = DiscoverAllTuners();
         BMenuItem* markedTuner = fTunerMenu->FindMarked();
         
         if (markedTuner != nullptr) {
             config->ip = markedTuner->Label();
             tunerAcquired = true;
         } else if (!foundTuners.empty()) {
             config->ip = foundTuners[0];
             tunerAcquired = true;
         }

         if (tunerAcquired) {
             fCountdownSecondsRemaining = std::atoi(targetDuration.c_str());
             fStatusLabel->SetText("Status: Recording stream...");
             fRecordButton->SetEnabled(false);
             fStopButton->SetEnabled(true);
             
             atomic_set(&gCancelRecording, 0);

             std::string baseDir = fSelectedDirectory;
             if (!baseDir.empty() && baseDir.back() != '/') {
                 baseDir += "/";
             }

             std::time_t rawTime = std::time(nullptr);
             std::tm* timeInfo = std::localtime(&rawTime);
             char timestampBuffer[64];
             std::strftime(timestampBuffer, sizeof(timestampBuffer), "%Y-%m-%d_%H-%M-%S", timeInfo);

             config->path = baseDir + "DVR_Record_Ch_" + config->channel + "_" 
                          + timestampBuffer + "_" + targetDuration + "s.ts";

             fActiveThread = spawn_thread(NetworkRecordingThread, "DVRStreamWorker", B_NORMAL_PRIORITY, config);
             if (fActiveThread >= B_OK) {
                 resume_thread(fActiveThread);
             } else {
                 fStatusLabel->SetText("Status: Thread Error!");
                 fRecordButton->SetEnabled(true);
                 fStopButton->SetEnabled(false);
                 delete config;
             }
         } else {
             delete config;
             fStatusLabel->SetText("Status: Error - No physical tuner detected!");
             fRecordButton->SetEnabled(true);
             fStopButton->SetEnabled(false);
         }
         break;
         
     }

        // =========================================================================
        // HANDLER A: TRIGGER THREAD SPINNING ACTION
        // =========================================================================
        case MSG_CHECK_FIRMWARE: {
            fStatusLabel->SetText("Status: Querying tuner hardware profile...");
            
            // Allocate our tracking parameter structure 
            FirmwareParam* threadArgs = new FirmwareParam();
            threadArgs->targetWindow = this;
            threadArgs->tunerIp = fSelectedIp.c_str(); // Feeds your live active IP selection straight down the pipe!
            
            // Spawn the thread, passing our parameter packet pointer as the data argument
            thread_id checkerThread = spawn_thread(FirmwareCheckerWorker, "TunerFirmwareTask", B_NORMAL_PRIORITY, threadArgs);
            if (checkerThread >= 0) {
                resume_thread(checkerThread);
            } else {
                fStatusLabel->SetText("Status: Error spawning firmware worker thread.");
                delete threadArgs; // Safety memory clean up if thread creation fails
            }
            break;
        }

        // =========================================================================
        // HANDLER B: PROCESS FINISHED THREAD CALCULATIONS AND DISPLAY NATIVE DIALOG
        // =========================================================================
        case MSG_FIRMWARE_CHECK_DONE: {
            BString statusError, currentVer, latestVer;
            
            if (message->FindString("status_text", &statusError) == B_OK) {
                fStatusLabel->SetText(statusError.String());
                BAlert* errAlert = new BAlert("Tuner Check", statusError.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
                errAlert->Go();
            } 
            else if (message->FindString("current_version", &currentVer) == B_OK &&
                     message->FindString("latest_version", &latestVer) == B_OK) {
                
                BString dialogDetails;
                dialogDetails << "Installed Tuner Firmware: " << currentVer << "\n";
                dialogDetails << "Latest Available Version: " << latestVer << "\n\n";
                
                if (currentVer == latestVer) {
                    dialogDetails << "Your HDHomeRun tuner hardware is completely up to date!";
                    BAlert* upToDateAlert = new BAlert("Tuner Firmware", dialogDetails.String(), "Awesome", NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
                    upToDateAlert->Go();
                    fStatusLabel->SetText("Status: Tuner firmware is up to date.");
                } else {
                    dialogDetails << "An important new firmware update is available for your device.";
                    BAlert* updateAlert = new BAlert("Update Available!", dialogDetails.String(), "Download Manually", "Close", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
                    
                    int32 userChoice = updateAlert->Go();
                    if (userChoice == 0) {
                        // =========================================================================
                        //  Route update workflow right into the tuner's local hardware page
                        // =========================================================================
                        BString localUpgradeUrl;
                        
                        // Use your active fSelectedIp variable to target your exact device
                        BString currentIp(fSelectedIp.c_str());
                        if (currentIp.IsEmpty()) {
                            currentIp = "127.0.0.1";
                        }
                        
                        // Formulate the path to the tuner's local administration/system panel
                        localUpgradeUrl.SetToFormat("http://%s/", currentIp.String());
                        
                        if (cfg.debugEnable) {
                            printf("[DEBUG FIRMWARE] Directing native browser onto local device: %s\n", 
                                   localUpgradeUrl.String());
                        }

                        // Bundle the local URL string as an argument payload
                        BMessage browserMsg(B_ARGV_RECEIVED);
                        browserMsg.AddString("argv", localUpgradeUrl.String());
                        
                        // Launch WebPositive (or default browser) directly onto the local hardware interface
                        be_roster->Launch("text/html", &browserMsg);
                    }
                    fStatusLabel->SetText("Status: Opening local tuner upgrade dashboard...");

                }
            }
            break;
        }



     case MSG_COUNTDOWN_TICK: {
         if (fActiveThread >= B_OK && fCountdownSecondsRemaining > 0) {
             fCountdownSecondsRemaining--;
             
             int32 mins = fCountdownSecondsRemaining / 60;
             int32 secs = fCountdownSecondsRemaining % 60;
             
             char timerBuffer[64];
             sprintf(timerBuffer, "Time Remaining: %02d:%02d", (int)mins, (int)secs);
             fCountdownLabel->SetText(timerBuffer);
         } else if (fCountdownSecondsRemaining <= 0 && fActiveThread >= B_OK) {
             PostMessage(MSG_STOP_RECORDING);
         }
         break;
     }

     case MSG_RECORDING_DONE:        
         fStatusLabel->SetText("Status: Recording complete/stopped.");
         fCountdownLabel->SetText("Time Remaining: --:--"); 
         fCountdownSecondsRemaining = 0;         
         fRecordButton->SetEnabled(true);
         fStopButton->SetEnabled(false);         
         fActiveThread = B_BAD_THREAD_ID; 
         break;        
       
     case MSG_STOP_RECORDING:

         fStatusLabel->SetText("Status: Stopping recording...");
         atomic_set(&gCancelRecording, 1);
         fStopButton->SetEnabled(false);
         break;
         

         
     default:
         BWindow::MessageReceived(message);
         break;
        }
    }
};

DVRWindow::~DVRWindow() {
    delete fFolderPanel;
    for (BBitmap* bitmap : fIconCache) {
            delete bitmap;
        }
    fIconCache.clear();
}

class DVRApplication : public BApplication {
public:
    DVRApplication() : BApplication("application/x-vnd.haikuhdhomerun-dvr-gui") {}
    
    void ReadyToRun() override {
        DVRWindow* window = new DVRWindow();
        window->Show();
        
        // AUTOMATION: Immediately post the command to the main window thread loop
        // so it pops the look-ahead guide open right at startup!
        window->PostMessage(MSG_OPEN_GUIDE);
    }

};


int main() {
	ensure_config_dir();
    DVRApplication app;
    app.Run();
    return 0;
}

             
