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
#include <OS.h>
#include <curl/curl.h>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <nlohmann/json.hpp>
#include "hdhomerun.h"

#include <stdio.h>
#include <stdlib.h>
#include <Notification.h>

using json = nlohmann::json;

const uint32 MSG_RESTART_BACKEND   = 'rstB';
const uint32 MSG_POLL_BACKEND      = 'polB';
const uint32 MSG_POPUP_CALENDAR    = 'popC';
const uint32 MSG_DATE_SELECTED     = 'dtSl';
const uint32 MSG_START_RECORDING   = 'recS';
const uint32 MSG_STOP_RECORDING    = 'recT';
const uint32 MSG_RECORDING_DONE    = 'recD';
const uint32 MSG_TUNER_SELECTED    = 'tunS';
const uint32 MSG_ADD_SCHEDULE      = 'schA';
const uint32 MSG_DURATION_SELECTED = 'durS';
const uint32 MSG_CHANNEL_CLICKED   = 'chCl';
const uint32 MSG_REFRESH_SCHEDULES = 'schR';
const uint32 MSG_REMOVE_SCHEDULE   = 'scRh';
const uint32 MSG_FILTER_ALL        = 'fltA';
const uint32 MSG_FILTER_HD         = 'fltH';
const uint32 MSG_FILTER_SD         = 'fltS';
const uint32 MSG_CHOOSE_DIR        = 'chDr';
const uint32 MSG_DIR_CHOSEN        = 'drCh';
const uint32 MSG_COUNTDOWN_TICK    = 'cdTk';
const uint32 MSG_CLOCK_UP          = 'clkU';
const uint32 MSG_CLOCK_DOWN    	   = 'clkD';
const uint32 MSG_DISK_SPACE_WARNING = 'dSpc';
const uint32 MSG_REFRESH_CHANNEL_LIST_ICONS = 'rIco';
const uint32 MSG_STREAM_PROGRESS_UPDATE = 'sPrg';
const uint32 MSG_TOGGLE_NOTIFICATIONS= 'ntfg';

const char* kSettingsFilePath = "/boot/home/config/settings/HaikuDVR_schedules.json";

void ensure_config_dir() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("HaikuDVR/icons");
        create_directory(path.Path(), 0755);
    }
}

// 1. CLEAN APPCONFIG STRUCT
struct AppConfig {
    bool showUpdateNotifications = true;
    bool debugEnable = true; 
};

AppConfig cfg; 

// 2. RUNTIME TRACKING FLAGS (Keep these out of AppConfig)
int32 gCancelRecording = 0;
int32 gStopScheduler = 0;

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
};

struct ChannelGuideItem {
    std::string guideNumber;
    std::string guideName;
    std::string nowPlaying; 
    std::vector<UpcomingShowItem> futureLineup; 
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
    static const char* const VERSION_STRING = "HaikuDVR v1.0.2 (Haiku OS)";
}

// =============================================================================
// NATIVE ASYNCHRONOUS UPDATE ENGINE IMPLEMENTATION (CURL ENGINE PASS)
// =============================================================================
static int32 BackgroundUpdateChecker(void* data) {
    // Wait a brief 5 seconds after application boot to allow UI rendering to finalize completely
    snooze(5000000); 

    if (cfg.debugEnable) printf("[DEBUG_UPDATE] Asynchronous curl update checker running...\n");

    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/cricket/refs/heads/main/VERSION";

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

    // Strip visual prefix formatting blocks out of the remote string if they exist
    remoteVersionStr.ReplaceAll("v.", ""); 
    remoteVersionStr.ReplaceAll("v", "");  
    
    if (remoteVersionStr.Length() > 0) {
		BString currentVersionStr = AppInfo::VERSION_STRING;
		if (cfg.debugEnable) printf("[DEBUG_UPDATE] Local AppInfo text before cleaning: '%s'\n", currentVersionStr.String());
		
		// 1. Find where the semantic version sequence starts (v1., v0., etc.)
		int32 vPos = currentVersionStr.IFindFirst("v");
		// Safely skip the word "Version" if it exists by checking if the 'v' is part of it
		if (vPos != B_ERROR && currentVersionStr.IFindFirst("Version") == vPos) {
		    // Find the NEXT 'v' after the word "Version"
		    vPos = currentVersionStr.IFindFirst("v", vPos + 7);
		}
		
		if (vPos != B_ERROR) {
		    // Drop everything before the real version prefix
		    currentVersionStr.Remove(0, vPos);
		}
		
		// 2. Safely strip the 'v.' or 'v' prefix now that the string starts with it
		currentVersionStr.ReplaceAll("v.", ""); 
		currentVersionStr.ReplaceAll("v", "");  
		
		// 3. Drop trailing metadata like "(Haiku OS)"
		int32 spacePos = currentVersionStr.FindFirst(" ");
		if (spacePos != B_ERROR) {
		    currentVersionStr.Truncate(spacePos); 
		}
		
		currentVersionStr.Trim();
		if (cfg.debugEnable) printf("[DEBUG_UPDATE] Cleaned local target string: '%s'\n", currentVersionStr.String());



        // Parse semantic versions down into flat integers for safe math checks
        int32 curMajor = 0, curMinor = 0, curRevision = 0;
        int32 remMajor = 0, remMinor = 0, remRevision = 0;

        sscanf(currentVersionStr.String(), "%d.%d.%d", &curMajor, &curMinor, &curRevision);
        sscanf(remoteVersionStr.String(), "%d.%d.%d", &remMajor, &remMinor, &remRevision);

        int32 currentFlattened = (curMajor * 10000) + (curMinor * 100) + curRevision;
        int32 remoteFlattened  = (remMajor * 10000) + (remMinor * 100) + remRevision;

        if (cfg.debugEnable) {
            printf("[DEBUG_UPDATE] Calculated values for math match -> Local: %d | Remote: %d\n", 
                   (int)currentFlattened, (int)remoteFlattened);
        }

        if (remoteFlattened > currentFlattened) {
            if (cfg.debugEnable) printf("[DEBUG_UPDATE] Update matched! Checking alert preference flags...\n");
            
            // =========================================================================
            // CHANNELS AUTO-HIDE PREFERENCE INTERCEPT
            // =========================================================================
            if (!cfg.showUpdateNotifications) {
                if (cfg.debugEnable) printf("[DEBUG_UPDATE] Suppressing desktop alert toast\n");
                return B_OK; // Break out cleanly and silently without throwing the alert box!
            }
            // =========================================================================

            // Native Haiku desktop notification banner toast window dispatch engine
            BNotification updateAlert(B_INFORMATION_NOTIFICATION);
            updateAlert.SetGroup("Cricket IRC");
            updateAlert.SetTitle("Update Available");
            
            BString alertContent;
            alertContent << "A newer version of Cricket is available! (v" << remoteVersionStr 
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
                        
                        for (size_t g = 1; g < guideArray.size() && g <= 3; g++) {
                            UpcomingShowItem futureShow;
                            futureShow.title = guideArray[g].value("Title", "To Be Announced");
                            
                            std::time_t startEpoch = guideArray[g].value("StartTime", 0);
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

        // 1. Create the new Options / Update Notifications Dropdown
        BMenu* optionsMenu = new BMenu("Options");
        
        BMessage* msgNotifyOn = new BMessage(MSG_TOGGLE_NOTIFICATIONS);
        msgNotifyOn->AddBool("enable", true);
        fNotifyOnItem = new BMenuItem("Enable Update Alerts", msgNotifyOn);

        BMessage* msgNotifyOff = new BMessage(MSG_TOGGLE_NOTIFICATIONS);
        msgNotifyOff->AddBool("enable", false);
        fNotifyOffItem = new BMenuItem("Disable Update Alerts", msgNotifyOff);

        // Synchronize visual checkboxes with your disk configuration variable
        fNotifyOnItem->SetMarked(cfg.showUpdateNotifications == true);
        fNotifyOffItem->SetMarked(cfg.showUpdateNotifications == false);


        optionsMenu->AddItem(fNotifyOnItem);
        optionsMenu->AddItem(fNotifyOffItem);
        
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
        fTimeInput = new BTextControl(BRect(20, 175, 260, 200), "time", "Start Time:", "12:00", NULL);
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
        fRestartBackendButton = new BButton(BRect(730, 365, 860, 395), "restart_backend", "Restart Backend", new BMessage(MSG_RESTART_BACKEND));
        fRestartBackendButton->SetToolTip("Warning: Restarting the backend service will immediately abort any active scheduled recording streams currently in progress!");
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
	             BMenu* filterMenu = menuBar->SubmenuAt(0);
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
    }
};

int main() {
	ensure_config_dir();
    DVRApplication app;
    app.Run();
    return 0;
}

             
