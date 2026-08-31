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
#include <MessageRunner.h>
#include <StorageKit.h>
#include <StringList.h>
#include <Screen.h>
#include <dirent.h>
#include <unistd.h>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <sqlite3.h>
#include <MessageFilter.h>
#include <InterfaceKit.h>
#include <SupportKit.h>


namespace AppInfo {
    static const char* const VERSION_STRING = "HaikuDVR v1.0.42 (Haiku OS)";
}

const uint32 MSG_TOGGLE_DLNA			    = 'dlna';
const uint32 MSG_ABOUT_WINDOW				= 'mabw';
const uint32 MSG_CLOCK_TICK_5MIN 			= 'clt5'; 
const uint32 MSG_EXECUTE_SEARCH   			= 'exsr';
const uint32 MSG_SEARCH_SELECTED		    = 'srsl';
const uint32 MSG_OPEN_SEARCH_POPUP    		= 'mosp';
const uint32 MSG_GUIDE_TOGGLE_FULLSCREEN    = 'gtfs';
const uint32 MSG_TOGGLE_FULLSCREEN 			= 'enfs';
const uint32 MSG_OPEN_CALENDAR_PANEL		= 'opcl';
const uint32 MSG_SHOW_DATE_PICKER			= 'sdpk';
const uint32 MSG_SET_PLAYER_MPV          	= 'pmpv';
const uint32 MSG_SET_PLAYER_HTV 			= 'sphv'; 
const uint32 MSG_SET_PLAYER_MEDIAPLAYER 	= 'pmed';
const uint32 MSG_SET_PLAYER_VLC         	= 'pvlc';
const uint32 MSG_ABORT_SPECIFIC_RECORDING 	= 'absp';
const uint32 MSG_REFRESH_LIBRARY 			= 'rflb';
const uint32 MSG_QUIT_ENTIRE_APP 			= 'qapp';
const uint32 MSG_SHOW_MAIN_SCHEDULER 		= 'shms';
const uint32 MSG_CLOSE_GUIDE_WINDOW 		= 'clgw';
const uint32 MSG_PLAY_RECORDING   			= 'play';
const uint32 MSG_DELETE_RECORDING 			= 'delt';
const uint32 MSG_RECORDINGS_CLOSED 			= 'rcls';
const uint32 MSG_VIEW_RECORDINGS  			= 'vrec';
const uint32 MSG_OPEN_GUIDE 				= 'opgd';
const uint32 MSG_GUIDE_CLOSED 				= 'gcls';
const uint32 MSG_PERIODIC_GUIDE_REFRESH 	= 'pgrf';
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
const uint32 MSG_CHANNEL_DOUBLE_CLICKED 	= 'chdc';
const uint32 MSG_REFRESH_SCHEDULES 			= 'schR';
const uint32 MSG_PLAY_IN_MPV		 		= 'pimv';
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
    bool dlnaEnable = true; 
    bool debugEnable = false; 
    bool fullscreenEnable = false;
    std::string defaultPlayer; 
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
    std::string showTitle;   
    std::string channelLabel;
    int32 dbIndexPosition;
    
};

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
    std::string showDescription; 
    bool        processed;
};


struct UpcomingShowItem {
    std::string title;
    std::string startTimeStr; 
    BString endTimeStr;  
    BString description;
    int32 durationMinutes; 
};

struct ChannelGuideItem {
    std::string guideNumber;
    std::string guideName;
    std::string nowPlaying; 
    int32 nowPlayingDurationMinutes; 
    std::vector<UpcomingShowItem> futureLineup; 
    std::string nowPlayingEndTimeStr;
    std::string nowPlayingDescription; 
};

struct GuideProgramBlock {
    BString title;
    BString timeDisplay;
    float cellWidthPixels; 
    int32 durationMinutes; 
    BString endTimeStr;  
    BString description; 
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
    
    jRoot["save_directory"]            = gGlobalSaveDirectory;     
    jRoot["show_update_notifications"] = cfg.showUpdateNotifications; 
    jRoot["dlna_enable"]               = cfg.dlnaEnable;
    jRoot["debug_enable"]              = cfg.debugEnable;
    jRoot["enable_fullscreen"]         = cfg.fullscreenEnable;
    jRoot["default_player"]            = cfg.defaultPlayer;
    
    json jSchedules = json::array();
    for (const auto& item : gScheduleList) {
        if (!item.processed) {
            jSchedules.push_back({
                {"date", item.startDate}, 
                {"time", item.startTime},
                {"channel", item.channel},
                {"duration", item.duration},
                {"processed", item.processed},       // Track state safely
                {"tuner_ip", item.tunerIp},
                {"show_title", item.showTitle},
                {"show_description", item.showDescription} // FIXED: Exports split catalog descriptions cleanly to JSON disk space
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





// Helper utility to generate absolute epoch time from the file strings
// Add this helper function at the top of your file if it's missing
static time_t CalculateEpoch(const std::string& dateStr, const std::string& timeStr) {
    std::string fullDateTimeStr = dateStr + " " + timeStr;
    std::tm tm_struct = {};
    std::istringstream ss(fullDateTimeStr);
    ss >> std::get_time(&tm_struct, "%Y-%m-%d %H:%M");
    if (!ss.fail()) {
        tm_struct.tm_isdst = -1; // Let the system handle DST transitions natively
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
            gGlobalSaveDirectory        = jIn.value("save_directory", "/boot/home");
            cfg.showUpdateNotifications = jIn.value("show_update_notifications", true);
            cfg.dlnaEnable              = jIn.value("dlna_enable", true);
            cfg.debugEnable             = jIn.value("debug_enable", false);
            cfg.fullscreenEnable 		= jIn.value("enable_fullscreen", false);
            cfg.defaultPlayer           = jIn.value("default_player", "mpv"); 
            
            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate    = entry.value("date", "2026-06-23"); 
                    item.startTime    = entry.value("time", "12:00");
                    item.channel      = entry.value("channel", "5.1");
                    //item.channelLabel = entry.value("channel_label", ""); 
                    item.duration     = entry.value("duration", "1800");
                    item.tunerIp      = entry.value("tuner_ip", ""); 
                    item.showTitle    = entry.value("show_title", "Unknown_Show");
                    
                    // FIXED: Read description strings safely out of object collections
                    item.showDescription = entry.value("show_description", "No description available.");
                    
                    item.processed    = entry.value("processed", false);   

                    // Added computation conversions for our layout math engine
                    item.durationSec  = std::atoll(item.duration.c_str());
                    item.epochStart   = CalculateEpoch(item.startDate, item.startTime);

                    gScheduleList.push_back(item);
                }
            }
        }
        else if (jIn.is_array()) {
            cfg.showUpdateNotifications = true;
            cfg.dlnaEnable              = true;
            cfg.debugEnable             = false;
            cfg.fullscreenEnable		= false;
            cfg.defaultPlayer           = "MPV"; 
            
            gScheduleList.clear();
            for (const auto& entry : jIn) {
                ScheduleItem item;
                item.startDate    = entry.value("date", "2026-06-23"); 
                item.startTime    = entry.value("time", "12:00");
                item.channel      = entry.value("channel", "5.1");
               // item.channelLabel = entry.value("channel_label", ""); 
                item.duration     = entry.value("duration", "1800");
                item.tunerIp      = entry.value("tuner_ip", "");
                item.showTitle    = entry.value("show_title", "Unknown_Show"); 
                
                // FIXED: Read description strings safely out of legacy arrays
                item.showDescription = entry.value("show_description", "No description available.");
                
                item.processed    = entry.value("processed", false);   

                // Added computation conversions for our layout math engine
                item.durationSec  = std::atoll(item.duration.c_str());
                item.epochStart   = CalculateEpoch(item.startDate, item.startTime);

                gScheduleList.push_back(item);
            }
        }
        
        gScheduleLocker.Unlock();
    } catch (...) {
        gScheduleLocker.Unlock();
    }
    file.close();
}







static int32 BackgroundUpdateChecker(void* data) {
    snooze(5000000); 

    if (cfg.debugEnable) printf("[DEBUG_UPDATE] Asynchronous curl update checker running...\n");

    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/HaikuDVR/refs/heads/main/VERSION";

    BString shellCmdString;
    #if defined(__x86_64__)
        shellCmdString.SetToFormat("curl -sL \"%s\"", targetUrl);
    #else
        shellCmdString.SetToFormat("curl-x86 -sL \"%s\"", targetUrl);
    #endif

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


extern size_t NetworkStringCallback(void* contents, size_t size, size_t nmemb, void* userp);


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
    FirmwareParam* param = (FirmwareParam*)data;
    if (param == nullptr) return B_ERROR;
    
    BWindow* window = param->targetWindow;
    BString targetIp = param->tunerIp;
    
    delete param;

    if (window == nullptr) return B_ERROR;

    const char* discoveryTarget = targetIp.IsEmpty() ? "AUTO" : targetIp.String();

    struct hdhomerun_device_t* hdDevice = hdhomerun_device_create_from_str(discoveryTarget, NULL);
    if (hdDevice == nullptr) {
        BMessage notifyMsg(MSG_FIRMWARE_CHECK_DONE);
        notifyMsg.AddString("status_text", "Firmware Error: No HDHomeRun hardware detected at target IP.");
        window->PostMessage(&notifyMsg);
        return B_OK;
    }

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

    BMessage finishMessage(MSG_FIRMWARE_CHECK_DONE);
    finishMessage.AddString("current_version", currentFirmware.String());
    finishMessage.AddString("latest_version", latestFirmware.String());
       
    window->PostMessage(&finishMessage);
    hdhomerun_device_destroy(hdDevice);
    return B_OK;
}


struct LocalRecordingItem {
    BString fileName;
    BString channel;
    BString date;
    BString time;
    BString showTitle;
};



class RecordingListItem : public BStringItem {
public:
    BString fFullFilePath;
    BString fFileNameOnly;

    RecordingListItem(const char* label, const char* fullPath, const char* fileName) 
        : BStringItem(label) {
        fFullFilePath = fullPath;
        fFileNameOnly = fileName;
    }
};



class RecordingsBrowserWindow : public BWindow {
public:
    RecordingsBrowserWindow(BRect frame, const char* recordingDirectory, BWindow* mainAppWindow) 
        : BWindow(frame, "Recorded Media Library", B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS) {
        
        ResizeTo(650, 450);
        fMainAppWindow = mainAppWindow;
        fRecordingDir = recordingDirectory;
        
        fRecordingsList = new BListView("recordingsListView", B_SINGLE_SELECTION_LIST);
        fRecordingsList->SetInvocationMessage(new BMessage(MSG_PLAY_RECORDING));
        
        BScrollView* scrollWrapper = new BScrollView("scrollRecs", fRecordingsList, 0, false, true);
        
        fTotalUsageLabel = new BStringView("totalUsageLabel", "Calculating library storage footprint...");
        fTotalUsageLabel->SetExplicitMinSize(BSize(200, 20));
        fTotalUsageLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 20));

        fRefreshButton = new BButton("refreshBtn", "Refresh Library", new BMessage(MSG_REFRESH_LIBRARY));
        fRefreshButton->SetExplicitMaxSize(BSize(120, 24)); 

        BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
            .SetInsets(12, 12, 12, 12)
            .Add(scrollWrapper)
            .AddGroup(B_HORIZONTAL, 10) 
                .Add(fTotalUsageLabel, 1.0) 
                .Add(fRefreshButton, 0.0)  
            .End()
            .SetExplicitMaxSize(BSize(800, B_SIZE_UNLIMITED))
        .End();

        BMessage pulseMsg(MSG_REFRESH_LIBRARY);
        bigtime_t oneSecondInterval = 1000000;
        
        fPulseTimer = new BMessageRunner(BMessenger(this), &pulseMsg, oneSecondInterval);

        _ScanAndParseDirectory();
    }


    bool QuitRequested() override {
        if (fMainAppWindow != nullptr) {
            fMainAppWindow->PostMessage(MSG_RECORDINGS_CLOSED);
        }
        return true;
    }

    void DispatchMessage(BMessage* message, BHandler* handler) override {
        if (message->what == B_MOUSE_DOWN) {
            int32 buttons = 0;
            BPoint point;
            message->FindInt32("buttons", &buttons);
            message->FindPoint("where", &point); 

            if (buttons == B_SECONDARY_MOUSE_BUTTON) {
                BPoint listPoint = fRecordingsList->ConvertFromParent(point);
                
                int32 index = fRecordingsList->IndexOf(listPoint);
                if (index >= 0) {
                    fRecordingsList->Select(index); 
                    
                    BPopUpMenu* menu = new BPopUpMenu("Context", false, false);
                    menu->AddItem(new BMenuItem("Watch Recording", new BMessage(MSG_PLAY_RECORDING)));
                    menu->AddItem(new BMenuItem("Delete Recording", new BMessage(MSG_DELETE_RECORDING)));
                    
                    BPoint screenPoint = fRecordingsList->ConvertToScreen(listPoint) + BPoint(2, 2);
                    BMenuItem* chosen = menu->Go(screenPoint, false, true, false);
                    if (chosen != nullptr && chosen->Message() != nullptr) {
                        PostMessage(chosen->Message()); 
                    }
                    delete menu;
                    return; 
                }
            }
        }
        BWindow::DispatchMessage(message, handler);
    }

    void MessageReceived(BMessage* message) override {
        switch (message->what) {
        	
            case MSG_REFRESH_LIBRARY: {
                _ScanAndParseDirectory();
                break;
            }
        	
            case MSG_PLAY_RECORDING: {
                int32 selection = fRecordingsList->CurrentSelection();
                if (selection >= 0) {
                    RecordingListItem* item = (RecordingListItem*)fRecordingsList->ItemAt(selection);
                    if (item != nullptr) {                       

                        if (cfg.defaultPlayer == "MediaPlayer") {
                            entry_ref fileRef;
                            BEntry entry(item->fFullFilePath.String());
                            if (entry.GetRef(&fileRef) == B_OK) {
                                be_roster->Launch(&fileRef);
                            }
                        } 

                        else {
                            const char* binaryPath = "/boot/system/bin/mpv";
                            if (cfg.defaultPlayer == "VLC") {
                                binaryPath = "/boot/system/bin/vlc";
                            }
                            // --- ADD hTV OPTION MATCHING ---
                            else if (cfg.defaultPlayer == "hTV") {
                                binaryPath = "/boot/system/bin/hTV";
                            }

                            pid_t processId = fork();
                            if (processId == 0) {

                                char* playerArgs[3];
                                playerArgs[0] = (char*)binaryPath;
                                playerArgs[1] = (char*)item->fFullFilePath.String();
                                playerArgs[2] = nullptr; 
                                
                                execv(playerArgs[0], playerArgs);
                                _exit(1); 
                            }
                        }
                        
                    }
                }
                break;
            }




            case MSG_DELETE_RECORDING: {
                int32 selection = fRecordingsList->CurrentSelection();
                if (selection >= 0) {
                    RecordingListItem* item = (RecordingListItem*)fRecordingsList->ItemAt(selection);
                    if (item != nullptr) {
                        BString alertText;
                        alertText << "Are you sure you want to permanently delete:\n\n" 
                                  << item->Text() << "?";
                        
                        BAlert* confirm = new BAlert("Delete Recording", alertText.String(), 
                            "Cancel", "Delete", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
                        confirm->SetShortcut(0, B_ESCAPE);
                        
                        if (confirm->Go() == 1) {

                            BMessage abortActiveStream(MSG_ABORT_SPECIFIC_RECORDING);
                            abortActiveStream.AddString("file_path", item->fFullFilePath.String());
                            
                            if (fMainAppWindow != nullptr) {
                                fMainAppWindow->PostMessage(&abortActiveStream);
                            }

                            snooze(50000); 

                            BEntry fileEntry(item->fFullFilePath.String());
                            if (fileEntry.Remove() == B_OK) {
                                delete fRecordingsList->RemoveItem(selection);
                                _ScanAndParseDirectory(); 
                            } else {
                                BAlert* error = new BAlert("Error", "Could not delete file from disk storage. File may be locked.", "OK");
                                error->Go();
                            }
                        }
                    }
                }
                break;
            }
            

            default:
                BWindow::MessageReceived(message);
                break;
        }
    }

private:
    BString _FormatFileSize(off_t bytes) {
        double size = (double)bytes;
        int unit = 0;
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        
        while (size >= 1024.0 && unit < 4) {
            size /= 1024.0;
            unit++;
        }
        
    BString result;
    result.SetToFormat("%.2f %s", size, units[unit]);
    return result;
    }
    
	BStringView* 	fTotalUsageLabel;
    BListView* 		fRecordingsList;
    BWindow*   		fMainAppWindow;
    BString    		fRecordingDir;
	BButton*   		fRefreshButton;
	BMessageRunner* fPulseTimer; 
	 
    void _ScanAndParseDirectory() {
        off_t totalAccumulatedBytes = 0;
        BDirectory directory(fRecordingDir.String());
        if (directory.InitCheck() != B_OK) return;

        std::vector<BString> filesOnDisk;

        BEntry entry;
        directory.Rewind();

        while (directory.GetNextEntry(&entry) == B_OK) {
            char name[B_FILE_NAME_LENGTH];
            if (entry.GetName(name) != B_OK) continue;

            BPath fullPath;
            entry.GetPath(&fullPath);
            BString pathString = fullPath.Path();
            filesOnDisk.push_back(pathString);

            off_t fileSize = 0;
            entry.GetSize(&fileSize);
            totalAccumulatedBytes += fileSize;

            // =========================================================================
            // METADATA PARSING ENGINE (DVR FILENAME EXTRACTION WITH AM/PM CYCLE)
            // =========================================================================
            BString rawName(name);
            BString expectedLabel;
            
            char chNum[32] = {0};
            int year = 0, month = 0, day = 0, hour = 0, minute = 0;
            
            // Scan fixed-width initial structural elements safely
            if (std::sscanf(rawName.String(), "DVR_Record_Ch_%31[^_]_%d-%d-%d_%d-%d_", 
                chNum, &year, &month, &day, &hour, &minute) >= 6) {
                
                // Construct prefix filter to determine precisely where the title segment starts
                BString prefixFilter;
                prefixFilter.SetToFormat("DVR_Record_Ch_%s_%04d-%02d-%02d_%02d-%02d_", 
                    chNum, year, month, day, hour, minute
                );
                
                BString titleExtractor(rawName);
                titleExtractor.RemoveFirst(prefixFilter);
                
                // Cut off trailing padding duration anchors: e.g., "_1800s_Padded.ts"
                int32 trailingAnchorIdx = titleExtractor.IFindLast("_");
                if (trailingAnchorIdx != B_ERROR) {
                    BString tailCheck = titleExtractor.String() + trailingAnchorIdx;
                    if (tailCheck.IFindFirst("Padded") != B_ERROR) {
                        titleExtractor.Truncate(trailingAnchorIdx);
                        trailingAnchorIdx = titleExtractor.IFindLast("_");
                        if (trailingAnchorIdx != B_ERROR) {
                            titleExtractor.Truncate(trailingAnchorIdx);
                        }
                    }
                }
                
                // Convert underscores to human-friendly spaces
                titleExtractor.ReplaceAll("_", " ");
                
                // CALCULATE 12-HOUR AM/PM PARAMETERS
                int32 displayHour = (hour > 12) ? (hour - 12) : ((hour == 0) ? 12 : hour);
                const char* periodMarker = (hour >= 12) ? "PM" : "AM";
                
                // Render fully unified metadata string line layout with AM/PM format
                expectedLabel.SetToFormat("%s (Ch %s) — %04d-%02d-%02d @ %d:%02d %s (%s)", 
                    titleExtractor.String(), chNum, year, month, day, 
                    displayHour, minute, periodMarker,
                    _FormatFileSize(fileSize).String()
                );
            } else {
                // Safeguard layout representation fallback if token templates are unmatched
                expectedLabel << name << " (" << _FormatFileSize(fileSize).String() << ")";
            }
            // =========================================================================

            bool alreadyExists = false;
            int32 itemCount = fRecordingsList->CountItems();
            
            for (int32 i = 0; i < itemCount; i++) {
                RecordingListItem* existingItem = (RecordingListItem*)fRecordingsList->ItemAt(i);
                
                if (existingItem != nullptr && existingItem->fFullFilePath == pathString) {
                    alreadyExists = true;
                    
                    if (BString(existingItem->Text()) != expectedLabel) {
                        existingItem->SetText(expectedLabel.String());
                        fRecordingsList->InvalidateItem(i); 
                    }
                    break;
                }
            }

            if (!alreadyExists) {
                RecordingListItem* newItem = new RecordingListItem(
                    expectedLabel.String(), 
                    pathString.String(), 
                    name
                );
                fRecordingsList->AddItem(newItem);
            }
        }

        for (int32 i = fRecordingsList->CountItems() - 1; i >= 0; i--) {
            RecordingListItem* item = (RecordingListItem*)fRecordingsList->ItemAt(i);
            if (item != nullptr) {
                bool found = false;
                for (const auto& diskPath : filesOnDisk) {
                    if (item->fFullFilePath == diskPath) { found = true; break; }
                }
                if (!found) {
                    delete fRecordingsList->RemoveItem(i);
                }
            }
        }

        if (fTotalUsageLabel != nullptr) {
            BString footerLabel;
            footerLabel << "Total Library Footprint Storage: " << _FormatFileSize(totalAccumulatedBytes).String();
            fTotalUsageLabel->SetText(footerLabel.String());
        }
    }

};


size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
struct AsyncIconDownloadConfig {
    BMessenger windowMessenger;
    std::string iconPath;
    std::string downloadUrl;
    int32 listRowIndex;
};

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

std::vector<DownloadQueueItem> gIconDownloadQueue;
BLocker gIconQueueLocker;
int32 gIconThreadRunning = 0;
BMessenger* gIconWindowMessenger = nullptr;


size_t StorageWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

int32 SerialIconDownloaderThread(void* data) {
    atomic_set(&gIconThreadRunning, 1);

    while (true) {
        DownloadQueueItem job;
        bool hasJob = false;

        gIconQueueLocker.Lock();
        if (!gIconDownloadQueue.empty()) {
            job = gIconDownloadQueue.front();
            gIconDownloadQueue.erase(gIconDownloadQueue.begin());
            hasJob = true;
        }
        gIconQueueLocker.Unlock();

        if (!hasJob) {
            break;
        }

        CURL* downloadCurl = curl_easy_init();
        if (downloadCurl) {

            {
                std::ofstream iconOut(job.iconPath.c_str(), std::ios::binary);
                if (iconOut.is_open()) {
                    curl_easy_setopt(downloadCurl, CURLOPT_URL, job.downloadUrl.c_str());
                    curl_easy_setopt(downloadCurl, CURLOPT_USERAGENT, "Mozilla/5.0 HaikuDVR/1.0");
                    curl_easy_setopt(downloadCurl, CURLOPT_WRITEFUNCTION, StorageWriteCallback);
                    curl_easy_setopt(downloadCurl, CURLOPT_WRITEDATA, &iconOut);
                    curl_easy_setopt(downloadCurl, CURLOPT_TIMEOUT, 6L);
                    
                    CURLcode res = curl_easy_perform(downloadCurl);
                    iconOut.close(); 
                    
                    if (res == CURLE_OK) {
                        if (gIconWindowMessenger && gIconWindowMessenger->IsValid()) {
                            BMessage completionMsg(MSG_REFRESH_CHANNEL_LIST_ICONS);
                            gIconWindowMessenger->SendMessage(&completionMsg);
                        }
                    } else {
                        std::remove(job.iconPath.c_str()); 
                    }
                }
            } 
            
            curl_easy_cleanup(downloadCurl);
        }
        snooze(40000); 
    }

    atomic_set(&gIconThreadRunning, 0);
    return 0;
}





enum {
    MSG_PREV_MONTH    = 'PRVM',
    MSG_NEXT_MONTH    = 'NXTM',
    MSG_CANCEL_WINDOW = 'CNCL'
};

class CalendarClickFilter : public BMessageFilter {
public:
    CalendarClickFilter(BWindow* targetWindow) 
        : BMessageFilter(B_PROGRAMMED_DELIVERY, B_ANY_SOURCE, B_MOUSE_UP),
          fWindow(targetWindow) {}

	filter_result Filter(BMessage* message, BHandler** target) override {
	    if (fWindow != nullptr) {
	        fWindow->PostMessage(MSG_DATE_SELECTED);
	    }
	    return B_DISPATCH_MESSAGE; 
	}


private:
    BWindow* fWindow;
};

class CalendarWindow : public BWindow {
	private:
	    std::vector<BBitmap*> fIconCache;
	    BPrivate::BCalendarView* fCalendar;
	    BMessenger fTargetMessenger;
	
	public:
		CalendarWindow(BPoint spawnPoint, BMessenger target) 
		    : BWindow(BRect(spawnPoint.x, spawnPoint.y, spawnPoint.x + 240, spawnPoint.y + 290), 
		              "Select Date", B_MODAL_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE) {
		    
		    fTargetMessenger = target;
		
		    BView* panel = new BView(Bounds(), "CalPanel", B_FOLLOW_ALL, B_WILL_DRAW);
		    panel->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));	
		    AddChild(panel);
		
		    // Month Navigation Buttons at the top
		    BButton* prevBtn = new BButton(BRect(10, 10, 40, 35), "prev", "<", new BMessage(MSG_PREV_MONTH));
		    BButton* nextBtn = new BButton(BRect(200, 10, 230, 35), "next", ">", new BMessage(MSG_NEXT_MONTH));
		    
		    // Text string view to show the current month/year context
		    BStringView* monthLabel = new BStringView(BRect(50, 15, 190, 35), "monthLabel", "");
		    monthLabel->SetAlignment(B_ALIGN_CENTER);
		
		    panel->AddChild(prevBtn);
		    panel->AddChild(monthLabel);
		    panel->AddChild(nextBtn);
		
		    // Calendar view positioned safely down to give navigation controls headroom
		    fCalendar = new BPrivate::BCalendarView(BRect(10, 45, 230, 240), "calendar");	        
		    
		    // Reconnected back to the main scheduling notification message
		    fCalendar->SetSelectionMessage(new BMessage(MSG_DATE_SELECTED));
		    fCalendar->SetInvocationMessage(new BMessage(MSG_DATE_SELECTED));	        
		    fCalendar->SetFlags(fCalendar->Flags() | B_NAVIGABLE | B_WILL_DRAW);	        
		    fCalendar->SetTarget(this);	
		    
		    // Apply click filter explicitly to calendar grid view to enable smooth single-clicks
		    fCalendar->AddFilter(new CalendarClickFilter(this));
		    panel->AddChild(fCalendar);
		    
		    // Add bottom OK and Cancel action buttons
		    BButton* cancelBtn = new BButton(BRect(10, 250, 115, 280), "cancel", "Cancel", new BMessage(MSG_CANCEL_WINDOW));
		    BButton* okBtn = new BButton(BRect(125, 250, 230, 280), "ok", "OK", new BMessage(MSG_DATE_SELECTED));
		    okBtn->MakeDefault(true);
		    
		    panel->AddChild(cancelBtn);
		    panel->AddChild(okBtn);
		    
		    // Synchronize label context match
		    UpdateMonthLabel();
		}

    void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case B_KEY_DOWN: {
                int8 byte;
                if (message->FindInt8("byte", &byte) == B_OK && byte == B_ESCAPE) {
                    PostMessage(MSG_CANCEL_WINDOW);
                    return;
                }
                BWindow::MessageReceived(message);
                break;
            }
            case MSG_PREV_MONTH: {
                BPrivate::BDate d = fCalendar->Date();
                int32 month = d.Month();
                int32 year = d.Year();
                
                // Force day parameter to 1 to guarantee a valid date construction
                if (month == 1) {
                    fCalendar->SetDate(BPrivate::BDate(year - 1, 12, 1));
                } else {
                    fCalendar->SetDate(BPrivate::BDate(year, month - 1, 1));
                }
                UpdateMonthLabel();
                break;
            }
            case MSG_NEXT_MONTH: {
                BPrivate::BDate d = fCalendar->Date();
                int32 month = d.Month();
                int32 year = d.Year();
                
                // Force day parameter to 1 to guarantee a valid date construction
                if (month == 12) {
                    fCalendar->SetDate(BPrivate::BDate(year + 1, 1, 1));
                } else {
                    fCalendar->SetDate(BPrivate::BDate(year, month + 1, 1));
                }
                UpdateMonthLabel();
                break;
            }

            case MSG_DATE_SELECTED: {
                BPrivate::BDate selectedDate = fCalendar->Date();            
                int year  = selectedDate.Year();
                int month = selectedDate.Month();
                int day   = selectedDate.Day();
                
                char dateBuffer[32];
                sprintf(dateBuffer, "%04d-%02d-%02d", year, month, day);

                BMessage reply(MSG_DATE_SELECTED);
                reply.AddString("date_string", dateBuffer);
                fTargetMessenger.SendMessage(&reply);

                this->Lock();
                this->Quit();
                break;
            }
            case MSG_CANCEL_WINDOW: {
                this->Lock();
                this->Quit();
                break;
            }
            default:
                BWindow::MessageReceived(message);
                break;
        }
    }

    void UpdateMonthLabel() {
        BStringView* monthLabel = dynamic_cast<BStringView*>(FindView("monthLabel"));
        if (monthLabel != nullptr) {
            const char* months[] = {
                "January", "February", "March", "April", "May", "June",
                "July", "August", "September", "October", "November", "December"
            };
            BPrivate::BDate d = fCalendar->Date();
            int32 month = d.Month();
            int32 year = d.Year();
            
            if (month >= 1 && month <= 12) {
                char buf[32];
                sprintf(buf, "%s %d", months[month - 1], year);
                monthLabel->SetText(buf);
            }
        }
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





class SearchResultItem : public BListItem {
public:
    BString fTitle;
    BString fChannel; 
    BString fStartTime;
    BString fEndTime;
    BString fDescription;
    
    BString fXmlChannelId;
    int64   fStartEpoch;
	int64   fEndEpoch;
    
    SearchResultItem(const char* title, const char* chan, const char* start, 
                     const char* end, const char* desc, const char* xmlId, int64 startEpoch, int64 endEpoch) {
        fTitle = title;
        fChannel = chan;
        fStartTime = start;
        fEndTime = end;
        fDescription = desc;
        fXmlChannelId = xmlId;
        fStartEpoch = startEpoch;
        fEndEpoch = endEpoch;
    }

    virtual void DrawItem(BView* owner, BRect itemRect, bool drawEverything) {
        if (IsSelected()) {
            owner->SetHighColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
        } else {
            owner->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        }
        owner->FillRect(itemRect);

        rgb_color titleColor = IsSelected() ? ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR) : ui_color(B_DOCUMENT_TEXT_COLOR);
        rgb_color metaColor = (ui_color(B_PANEL_BACKGROUND_COLOR).red + ui_color(B_PANEL_BACKGROUND_COLOR).green + ui_color(B_PANEL_BACKGROUND_COLOR).blue > 384)
                              ? rgb_color{100, 100, 100, 255} : rgb_color{170, 170, 170, 255};

        owner->SetHighColor(titleColor);
        owner->SetFont(be_bold_font);
        owner->MovePenTo(itemRect.left + 10, itemRect.top + 16);
        owner->DrawString(fTitle.String());

        owner->SetHighColor(metaColor);
        owner->SetFont(be_plain_font);
        owner->MovePenTo(itemRect.left + 10, itemRect.top + 32);
        
        if (fStartTime == "via_combined") {
            owner->DrawString(fChannel.String());
        } else {
            BString legacyLine;
            legacyLine.SetToFormat("Ch %s | %s - %s", fChannel.String(), fStartTime.String(), fEndTime.String());
            owner->DrawString(legacyLine.String());
        }
    }



    virtual void Update(BView* owner, const BFont* font) {
        BListItem::Update(owner, font);
        SetHeight(40.0f); 
    }
    
private:
    bool is_repository_light_theme() {
        rgb_color panelColor = ui_color(B_PANEL_BACKGROUND_COLOR);
        return (panelColor.red + panelColor.green + panelColor.blue) > (128 * 3);
    }
};




class ProgramSearchWindow : public BWindow {
public:
    ProgramSearchWindow(BWindow* parentMessengerTarget)
        : BWindow(BRect(150, 150, 650, 550), "Program Guide Search", B_TITLED_WINDOW, 
                  B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS) 
    {
        fParentTarget = parentMessengerTarget;
        BView* rootPanel = new BView(Bounds(), "searchRoot", B_FOLLOW_ALL, B_WILL_DRAW);
        rootPanel->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        AddChild(rootPanel);

        fSearchBox = new BTextControl(BRect(10, 10, 390, 35), "searchQuery", 
                                      "Search Title:", "", new BMessage(MSG_EXECUTE_SEARCH));
        fSearchBox->SetModificationMessage(new BMessage(MSG_EXECUTE_SEARCH)); 
        rootPanel->AddChild(fSearchBox);

        BRect listFrame(10, 45, Bounds().Width() - 26, Bounds().Height() - 15);
        fResultsList = new BListView(listFrame, "resultsView", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
        fResultsList->SetSelectionMessage(new BMessage(MSG_SEARCH_SELECTED));
        
        BScrollView* scrollerShelf = new BScrollView("listScroller", fResultsList, 
                                                    B_FOLLOW_ALL, 0, false, true);
        rootPanel->AddChild(scrollerShelf);

        fSearchBox->MakeFocus(true);

        CenterOnScreen();
    }


    virtual void MessageReceived(BMessage* message) {
        switch (message->what) {
            case MSG_EXECUTE_SEARCH: {
                _PerformDatabaseQuery(fSearchBox->Text());
                break;
            }
            case MSG_SEARCH_SELECTED: {
                int32 selectionIdx = fResultsList->CurrentSelection();
                if (selectionIdx >= 0) {
                    SearchResultItem* selectedItem = (SearchResultItem*)fResultsList->ItemAt(selectionIdx);
                    if (selectedItem != nullptr && fParentTarget != nullptr) {
                        BMessage selectionNotice(MSG_SEARCH_SELECTED);
                        selectionNotice.AddString("title", selectedItem->fTitle);
                        selectionNotice.AddString("description", selectedItem->fDescription);
                        selectionNotice.AddString("channel", selectedItem->fChannel);
                        selectionNotice.AddString("channel_id", selectedItem->fXmlChannelId);
                        selectionNotice.AddInt64("start_epoch", selectedItem->fStartEpoch);
                        selectionNotice.AddInt64("end_epoch", selectedItem->fEndEpoch); 
                        // =========================================================================
                        
                        fParentTarget->PostMessage(&selectionNotice);
                    }
                }
                break;
            }

            default:
                BWindow::MessageReceived(message);
                break;
        }
    }


private:
    BWindow*       fParentTarget;
    BTextControl*  fSearchBox;
    BListView*     fResultsList;

    void _PerformDatabaseQuery(const char* userQuery) {
        // Purge old results list heap memory references safely
        for (int32 i = fResultsList->CountItems() - 1; i >= 0; i--) {
            delete fResultsList->RemoveItem(i);
        }

        BString cleanedQuery(userQuery);
        cleanedQuery.Trim();
        if (cleanedQuery.Length() < 2) return; 

        sqlite3* db = nullptr;
        const char* dbPath = "/boot/home/config/settings/HaikuDVR/guide.db";
        
        if (sqlite3_open(dbPath, &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return;
        }

        // =========================================================================
        // UNIFIED SCHEMA JOIN & TIME CONVERSION QUERY (WITH DURATION TRACKERS)
        // =========================================================================
        const char* sqlTemplate = 
            "SELECT p.title, c.lcn, "
            "       time(p.start_epoch, 'unixepoch', 'localtime') AS start_time, "
            "       time(p.end_epoch, 'unixepoch', 'localtime') AS end_time, "
            "       p.desc, "
            "       strftime('%Y-%m-%d', p.start_epoch, 'unixepoch', 'localtime') AS air_date, "
            "       p.channel_id, p.start_epoch, p.end_epoch " 
            "FROM programs p "
            "LEFT JOIN channels c ON p.channel_id = c.xml_id "
            "WHERE lower(p.title) LIKE lower(?) "
            "ORDER BY p.start_epoch ASC LIMIT 100;";
        
        sqlite3_stmt* statement = nullptr;
        int prepareResult = sqlite3_prepare_v2(db, sqlTemplate, -1, &statement, nullptr);
        
        if (prepareResult == SQLITE_OK) {
            BString bindPattern;
            bindPattern.SetToFormat("%%%s%%", cleanedQuery.String());
            sqlite3_bind_text(statement, 1, bindPattern.String(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(statement) == SQLITE_ROW) {
                const char* title   = (const char*)sqlite3_column_text(statement, 0);
                const char* lcn     = (const char*)sqlite3_column_text(statement, 1);
                const char* start   = (const char*)sqlite3_column_text(statement, 2);
                const char* end     = (const char*)sqlite3_column_text(statement, 3);
                const char* desc    = (const char*)sqlite3_column_text(statement, 4);
                const char* airDate = (const char*)sqlite3_column_text(statement, 5);
                const char* xmlId   = (const char*)sqlite3_column_text(statement, 6);
                int64 startEpoch    = (int64)sqlite3_column_int64(statement, 7);
                int64 endEpoch      = (int64)sqlite3_column_int64(statement, 8);

                // Convert SQLite's standard 24-hour time string ("HH:MM:SS") to clean 12-hour AM/PM format
                BString cleanStart = "--:--", cleanEnd = "--:--";
                int hStart = 0, mStart = 0, hEnd = 0, mEnd = 0;
                
                // Prepend the calendar date cleanly if available to separate repetitive listings
                BString prefixDate = (airDate != nullptr) ? airDate : "";
                if (!prefixDate.IsEmpty()) {
                    prefixDate << " @ ";
                }

                if (start && std::sscanf(start, "%d:%d", &hStart, &mStart) == 2) {
                    int dispH = (hStart > 12) ? (hStart - 12) : ((hStart == 0) ? 12 : hStart);
                    cleanStart.SetToFormat("%s%d:%02d %s", prefixDate.String(), dispH, mStart, (hStart >= 12 ? "PM" : "AM"));
                }
                if (end && std::sscanf(end, "%d:%d", &hEnd, &mEnd) == 2) {
                    int dispH = (hEnd > 12) ? (hEnd - 12) : ((hEnd == 0) ? 12 : hEnd);
                    cleanEnd.SetToFormat("%d:%02d %s", dispH, mEnd, (hEnd >= 12 ? "PM" : "AM"));
                }

                // Pass baseline parameters directly matching SearchResultItem specs perfectly
                fResultsList->AddItem(new SearchResultItem(
                    title ? title : "Unknown Title",
                    lcn   ? lcn   : "??",
                    cleanStart.String(),
                    cleanEnd.String(),
                    desc  ? desc  : "",
                    xmlId ? xmlId : "",
                    startEpoch,
                    endEpoch
                ));
            }
        } else {
            printf("[DVR Search Debug] SQL Compilation Failed: %s\n", sqlite3_errmsg(db));
        }
        
        sqlite3_finalize(statement);
        sqlite3_close(db);
    }

};


// =========================================================================
//  HEADER VIEW WITH PERFECTLY CENTERED GLYPHS AND MELLOW HOVER GLOWS
// =========================================================================
class TimelineHeaderView : public BView {
private:
    BRect fDateClickRect;
    BRect fTimeDownRect;
    BRect fTimeUpRect;
    BWindow* fMainAppTarget;
    BRect fSearchClickRect;
    bool fHoveringMinus;
    bool fHoveringPlus;

    void DrawCenteredGlyph(const char* glyph, BRect rect, BFont* font) {
        float stringWidth = font->StringWidth(glyph);
        
        font_height fh;
        font->GetHeight(&fh);
        float stringHeight = fh.ascent + fh.descent;

        float xOffset = rect.left + ((rect.Width() - stringWidth) / 2.0f);
        float yOffset = rect.top + fh.ascent + ((rect.Height() - stringHeight) / 2.0f);

        MovePenTo(xOffset, yOffset);
        DrawString(glyph);
    }


public:
    BString fCachedSelectedDate;
    BString fCachedSelectedTime;
    TimelineHeaderView(BRect frame, BWindow* mainAppTarget) 
        : BView(frame, "timelineHeader", B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW | B_NAVIGABLE) {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        
        fMainAppTarget = mainAppTarget;
        fHoveringMinus = false;
        fHoveringPlus = false;
        
        fCachedSelectedTime = ""; 
        fCachedSelectedDate = "";
        
        fDateClickRect.Set(90.0, 0.0, 185.0, frame.Height());
        fTimeDownRect.Set(200.0, 8.0, 222.0, 32.0);
        fTimeUpRect.Set(230.0, 8.0, 252.0, 32.0);

        fDateClickRect.Set(90.0, 0.0, 185.0, frame.Height());
        fTimeDownRect.Set(200.0, 8.0, 222.0, 32.0);
        fTimeUpRect.Set(230.0, 8.0, 252.0, 32.0);

        fSearchClickRect.Set(270.0f, 0.0f, 450.0f, frame.Height());

        SetEventMask(B_POINTER_EVENTS, 0);
    }

    void MouseMoved(BPoint point, uint32 transit, const BMessage* message) override {
        bool oldHoverMinus = fHoveringMinus;
        bool oldHoverPlus = fHoveringPlus;

        fHoveringMinus = fTimeDownRect.Contains(point);
        fHoveringPlus = fTimeUpRect.Contains(point);

        if (fHoveringMinus != oldHoverMinus || fHoveringPlus != oldHoverPlus) {
            Invalidate(fTimeDownRect);
            Invalidate(fTimeUpRect);
        }
        BView::MouseMoved(point, transit, message);
    }

    void Draw(BRect updateRect) override {
        BRect bounds = Bounds();
        rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
        rgb_color gridLineColor = ui_color(B_CONTROL_BORDER_COLOR);
        rgb_color accentColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR); 
        
        rgb_color softGlowColor = { 80, 80, 80, 255 };
        
        const float kHeaderWidth = 300.0;
        const float kCellWidth = 350.0;

        SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        FillRect(bounds);

        // Standard Label Typography
        BFont labelFont;
        GetFont(&labelFont);
        labelFont.SetFace(B_BOLD_FACE);
        labelFont.SetSize(10.0);
        SetFont(&labelFont);

        // Left sidebar rendering
        SetHighColor(textColor);
        MovePenTo(16.0, bounds.top + 24);
        DrawString("CHANNELS");

        SetHighColor(gridLineColor);
        MovePenTo(88.0, bounds.top + 24);
        DrawString("|");

        SetHighColor(textColor);
        MovePenTo(102.0, bounds.top + 24);
        DrawString("CALENDAR");

        BPoint arrowCenter(168.0, bounds.top + 20);
        FillTriangle(BPoint(arrowCenter.x - 4, arrowCenter.y - 2),
                     BPoint(arrowCenter.x + 4, arrowCenter.y - 2),
                     BPoint(arrowCenter.x,     arrowCenter.y + 3));

        SetHighColor(accentColor);
        StrokeLine(BPoint(102.0, bounds.bottom - 4), BPoint(175.0, bounds.bottom - 4));

        // =========================================================================
        // RENDERING ENGINE FOR STEP BUTTONS (WITH CENTER MATH & MELLOW GLOW)
        // =========================================================================
        BFont glyphFont;
        GetFont(&glyphFont);
        glyphFont.SetFace(B_BOLD_FACE);
        glyphFont.SetSize(14.0);
        SetFont(&glyphFont);

        // 1. Draw Minus Button
        if (fHoveringMinus) {
            SetHighColor(softGlowColor);
            FillRect(fTimeDownRect);             
            SetHighColor(textColor);
        } else {
            SetHighColor(gridLineColor);
            StrokeRect(fTimeDownRect);
            SetHighColor(textColor);
        }
        DrawCenteredGlyph("-", fTimeDownRect, &glyphFont);

        // 2. Draw Plus Button
        if (fHoveringPlus) {
            SetHighColor(softGlowColor);
            FillRect(fTimeUpRect);               
            SetHighColor(textColor);
        } else {
            SetHighColor(gridLineColor);
            StrokeRect(fTimeUpRect);
            SetHighColor(textColor);
        }
        DrawCenteredGlyph("+", fTimeUpRect, &glyphFont);
		
   		// =========================================================================
        // TIMELINE RENDERING 
        // =========================================================================
        SetFont(&labelFont);
        SetHighColor(gridLineColor);
        StrokeLine(BPoint(kHeaderWidth, bounds.top), BPoint(kHeaderWidth, bounds.bottom));

        float currentLeft = kHeaderWidth + 1.0;
        
        std::time_t realSysTime = std::time(nullptr);
        std::tm* sysTm = std::localtime(&realSysTime);
        
        char liveDateBuf[32] = {0};
        char liveTimeBuf[32] = {0};
        std::strftime(liveDateBuf, sizeof(liveDateBuf), "%Y-%m-%d", sysTm);
        std::strftime(liveTimeBuf, sizeof(liveTimeBuf), "%I:%M %p", sysTm);
        
        BString cleanLiveTime(liveTimeBuf);
        if (cleanLiveTime.StartsWith("0")) {
            cleanLiveTime.Remove(0, 1);
        }

        const char* timeIntervals[] = { "DASHBOARD_MONITOR", "+ 30 MINS", "+ 1.0 HOUR", "+ 1.5 HOURS" };

        for (int i = 0; i < 4; i++) {
            if (i == 0) {
                BFont monitorFont;
                GetFont(&monitorFont);
                monitorFont.SetFace(B_BOLD_FACE);
                monitorFont.SetSize(9.5f); 
                SetFont(&monitorFont);

                // =========================================================================
                // 1. CONVERT COMBINED SELECTED TIME STRING TO 12-HOUR AM/PM FORMAT
                // =========================================================================
                BString processedSelectedTime = fCachedSelectedTime;
                
                int selHour = 12, selMin = 0; 
                
                if (std::sscanf(processedSelectedTime.String(), "%d:%d", &selHour, &selMin) == 2) {
                    char ampmBuf[16];
                    std::snprintf(ampmBuf, sizeof(ampmBuf), "%s", (selHour >= 12 ? "PM" : "AM"));
                    
                    int displayHour = (selHour > 12) ? (selHour - 12) : ((selHour == 0) ? 12 : selHour);
                    processedSelectedTime.SetToFormat("%d:%02d %s", displayHour, selMin, ampmBuf);
                }
				

                PushState();
                SetHighColor(rgb_color{255, 255, 255, 255}); 
                
                BFont searchFont;
                GetFont(&searchFont);
                searchFont.SetFace(B_REGULAR_FACE);
                
                searchFont.SetSize(13.5f);
                SetFont(&searchFont);

                MovePenTo(310.0f, bounds.top + 26.0f);
                DrawString("🔍 Search Program Guide...");
                PopState();

                SetFont(&labelFont);

            } else {
                SetHighColor(textColor);
                MovePenTo(currentLeft + 20, bounds.top + 24);
                DrawString(timeIntervals[i]);
            }

            SetHighColor(gridLineColor);
            StrokeLine(BPoint(currentLeft + kCellWidth - 12, bounds.top + 8), 
                       BPoint(currentLeft + kCellWidth - 12, bounds.bottom));
            
            currentLeft += kCellWidth;
        }
		
        SetHighColor(gridLineColor);
        StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
    }

	void MouseDown(BPoint point) override {
	    // 1. Safety check: Ignore all clicks if the window is missing, 
	    // inactive (lacks focus), or minimized.
	    if (!Window() || !Window()->IsActive() || Window()->IsMinimized()) {
	        return;
	    }
	
	    if (fMainAppTarget != nullptr) {
	        BMessenger targetMessenger(fMainAppTarget);
	        BRect bounds = Bounds();
	
	        if (fDateClickRect.Contains(point)) {
	            BPoint dropPoint = ConvertToScreen(BPoint(fDateClickRect.left, fDateClickRect.bottom));
	            CalendarWindow* calWin = new CalendarWindow(dropPoint, targetMessenger);
	            calWin->Show();
	        }
	        else if (fTimeDownRect.Contains(point)) {
	            BMessage msg(MSG_CLOCK_DOWN);
	            targetMessenger.SendMessage(&msg);
	        }
	        else if (fTimeUpRect.Contains(point)) {
	            BMessage msg(MSG_CLOCK_UP);
	            targetMessenger.SendMessage(&msg);
	        }
	        else if (point.x >= 300.0f && point.x <= 550.0f && 
	                 point.y >= 0.0f && point.y <= bounds.Height()) {
	            
	            BMessage msg(MSG_OPEN_SEARCH_POPUP);
	            targetMessenger.SendMessage(&msg);
	        }
	        else {
	            // Only pass to the base class if the window is in focus 
	            // but the click didn't match any specific zones above
	            BView::MouseDown(point);
	        }
	    }
	}


    
    void MessageReceived(BMessage* message) override {
        switch (message->what) {
        	
 			case 'UCLT': { 
                const char* newTime = nullptr;
                const char* newDate = nullptr;
                
                if (message->FindString("time", &newTime) == B_OK && newTime != nullptr) {
                    fCachedSelectedTime.SetTo(newTime);
                }
                if (message->FindString("date", &newDate) == B_OK && newDate != nullptr) {
                    fCachedSelectedDate.SetTo(newDate);
                }
                
                Invalidate(); 
                break;
            }
            default:
                BView::MessageReceived(message);
                break;
        }
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
        // =========================================================================
        // NATIVE QUICK VIEW TEXT SCRUBBER (HTML ENTITY DECODER ENGINE)
        // =========================================================================
        // Decodes HTML data strings natively right at the moment of row creation
        BString scrubbed(text ? text : "");
        scrubbed.ReplaceAll("&amp;",  "&");
        scrubbed.ReplaceAll("&quot;", "\"");
        scrubbed.ReplaceAll("&apos;", "'");
        scrubbed.ReplaceAll("&#39;",  "'");
        scrubbed.ReplaceAll("&lt;",   "<");
        scrubbed.ReplaceAll("&gt;",   ">");
        
        textDisplay = scrubbed.String(); // Assign clean plain text to your display string
        // =========================================================================
        
        channelIcon = cachedIcon;
    }

    virtual ~ChannelListItem() {
        // Leaving this empty is correct because fIconCache or the item list
        // owns the bitmap lifecycle, preventing double-free crashes.
    }

    void DrawItem(BView* owner, BRect itemRect, bool drawEverything) override {
        rgb_color bgColor;
        rgb_color textColor;

        if (IsSelected()) {
            bgColor = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);
            textColor = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
        } else {
            bgColor = ui_color(B_PANEL_BACKGROUND_COLOR); 
            textColor = ui_color(B_PANEL_TEXT_COLOR); 
        }

        owner->SetLowColor(bgColor);
        owner->SetHighColor(bgColor);
        owner->FillRect(itemRect, B_SOLID_HIGH);

        float iconOffset = 5.0;
        if (channelIcon != nullptr) {
            BRect destRect(itemRect.left + 5, itemRect.top + 1, itemRect.left + 27, itemRect.top + 23);            
            drawing_mode oldMode = owner->DrawingMode();
            
            owner->SetDrawingMode(B_OP_ALPHA);            
            owner->DrawBitmap(channelIcon, channelIcon->Bounds(), destRect, B_FILTER_BITMAP_BILINEAR);            
            owner->SetDrawingMode(oldMode);            
            
            iconOffset = 32.0; 
        }

        font_height fh;
        owner->GetFontHeight(&fh);
        float textHeight = fh.ascent + fh.descent;
        float middleOfRow = itemRect.top + (itemRect.Height() / 2.0f);
        float baselineY = middleOfRow - (textHeight / 2.0f) + fh.ascent;

        owner->SetHighColor(textColor);
        owner->MovePenTo(itemRect.left + iconOffset, baselineY);
        owner->DrawString(textDisplay.c_str());
    }
};




// =========================================================================
//  LIST ITEM WITH HOVER TRACKING AND CELL CLICK INTERFACE (FIXED FUTURE MONTHS)
// =========================================================================
class GuideListRowItem : public BListItem {
public:
    GuideRowModel fData;
    int32 fRowIndex;
    int32 fHoveredCellIndex; 

	bool fCellIsScheduledMap[4] = {false, false, false, false}; 

    GuideListRowItem(GuideRowModel data, int32 index) 
        : BListItem(), fData(data), fRowIndex(index), fHoveredCellIndex(-1) {
        SetHeight(140); 
    }

    void Update(BView* owner, const BFont* font) override {
        BListItem::Update(owner, font);
        SetHeight(140); 
    }

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
            float relativeX = localX - kHeaderWidth;
            int32 targetedColumn = (int32)(relativeX / kCellWidth);
            
            if (targetedColumn >= 0 && targetedColumn < 4 && (size_t)targetedColumn < fData.programs.size()) {
                float columnLeftEdge = targetedColumn * kCellWidth;
                
                float cardRightEdge = columnLeftEdge + (kCellWidth - 12.0f);
                
                if (relativeX >= columnLeftEdge && relativeX <= cardRightEdge) {
                    cellIndex = targetedColumn;
                }
            }
        }

        if (fHoveredCellIndex != cellIndex) {
            fHoveredCellIndex = cellIndex;
            owner->Invalidate(itemRect); 
        }
    }

    void HandleDoubleClick(BView* owner, BPoint point, BRect itemRect, BWindow* parentWindow) {
        const float kHeaderWidth = 300.0;
        const float kCellWidth = 350.0;

        float localX = point.x - itemRect.left;
        if (localX > kHeaderWidth) {
            int32 targetCellIndex = -1;
            float currentLeft = kHeaderWidth + 1.0f;

            for (size_t idx = 0; idx < fData.programs.size(); idx++) {
                bool isContinuation = false;
                for (int32 prev = (int32)idx - 1; prev >= 0; prev--) {
                    if (fData.programs[prev].title == fData.programs[idx].title) {
                        isContinuation = true;
                        break;
                    }
                }
                if (isContinuation) continue;

                int32 cellsToSpan = 1;
                for (size_t next = idx + 1; next < fData.programs.size(); next++) {
                    if (fData.programs[next].title == fData.programs[idx].title) {
                        cellsToSpan++;
                    } else {
                        break;
                    }
                }

                float spannedCellWidth = cellsToSpan * kCellWidth;
                if (localX >= currentLeft && localX < (currentLeft + spannedCellWidth)) {
                    targetCellIndex = (int32)idx;
                    break;
                }
                currentLeft += kCellWidth;
            }

            if (targetCellIndex >= 0 && targetCellIndex < (int32)fData.programs.size()) {
                const auto& selectedProg = fData.programs[targetCellIndex];

                BMessage selectionBroadcast(MSG_PREFILL_RECORD_SCHEDULE);
                selectionBroadcast.AddString("show_title", selectedProg.title.String());                
                selectionBroadcast.AddString("show_description", selectedProg.description.String());                
                selectionBroadcast.AddString("channel_label", fData.channelLabel.String());
                selectionBroadcast.AddInt32("duration_minutes", selectedProg.durationMinutes);
                
                BString targetSubchannel = fData.channelLabel;
                int32 spaceIndex = targetSubchannel.FindFirst(" ");
                if (spaceIndex != B_ERROR) {
                    targetSubchannel.Truncate(spaceIndex);
                }
                targetSubchannel.Trim();
                selectionBroadcast.AddString("numeric_subchannel", targetSubchannel.String());

                // =========================================================================
                // EXTRACT CALENDAR DATE & NORMALIZE TIMESTAMPS TO 24-HOUR FORMAT
                // =========================================================================
                BString activeSelectedDateStr = "";
                if (parentWindow != nullptr) {
                    BTextControl* dateInput = dynamic_cast<BTextControl*>(parentWindow->FindView("date_input"));
                    if (dateInput != nullptr && dateInput->Text() != nullptr) {
                        activeSelectedDateStr = dateInput->Text();
                    }
                }

                std::string rawDisplayTime = selectedProg.timeDisplay.String();
                std::string standardized24HourTime = "";
                int hours = 0, minutes = 0;
                char ampm[16] = {0};
                
                if (std::sscanf(rawDisplayTime.c_str(), "%d:%d %15s", &hours, &minutes, ampm) >= 2) {
                    std::string ampmStr(ampm);
                    if (ampmStr.find("PM") != std::string::npos || ampmStr.find("pm") != std::string::npos) {
                        if (hours < 12) hours += 12;
                    } else if (ampmStr.find("AM") != std::string::npos || ampmStr.find("am") != std::string::npos) {
                        if (hours == 12) hours = 0;
                    }
                    char normBuffer[32];
                    std::snprintf(normBuffer, sizeof(normBuffer), "%02d:%02d", hours, minutes);
                    standardized24HourTime = normBuffer;
                } else {
                    standardized24HourTime = rawDisplayTime; 
                }


                // Pack both formats so all backend versions extract it flawlessly
                selectionBroadcast.AddString("start_time", standardized24HourTime.c_str());
                if (!activeSelectedDateStr.IsEmpty()) {
                    selectionBroadcast.AddString("date_string", activeSelectedDateStr.String());
                    selectionBroadcast.AddString("start_date", activeSelectedDateStr.String());
                }

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
        
        owner->SetHighColor(panelBg);
        owner->FillRect(itemRect);

        const float kHeaderWidth = 300.0; 
        const float kCellWidth = 350.0;

        BRect headerRect(itemRect.left, itemRect.top, itemRect.left + kHeaderWidth, itemRect.bottom - 1);
        owner->SetHighColor(panelBg);
        owner->FillRect(headerRect);
        
        float iconOffset = 16.0;
        if (fData.channelIcon != nullptr) {
            float topOffset = itemRect.top + 21.0; 
            BRect iconBounds = fData.channelIcon->Bounds();
            float origWidth = iconBounds.Width() + 1.0;
            float origHeight = iconBounds.Height() + 1.0;
            float targetHeight = 98.0;
            float targetWidth = origWidth;
            
            if (origHeight > 0) {
                targetWidth = (origWidth / origHeight) * targetHeight;
            }
            if (targetWidth > 150.0) {
                targetWidth = 150.0;
                targetHeight = (origHeight / origWidth) * targetWidth;
            }

            BRect destRect(itemRect.left + 16, topOffset + ((98.0 - targetHeight) / 2.0), 
                           itemRect.left + 16 + targetWidth, topOffset + ((98.0 - targetHeight) / 2.0) + targetHeight);
            
            drawing_mode oldMode = owner->DrawingMode();
            owner->SetDrawingMode(B_OP_ALPHA);
            owner->DrawBitmap(fData.channelIcon, iconBounds, destRect, B_FILTER_BITMAP_BILINEAR);
            owner->SetDrawingMode(oldMode);
            iconOffset = 16.0 + targetWidth + 16.0; 
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
        // PRE-CALCULATIONS & ROBUST VIEW TREE EXTRACTS
        // =========================================================================
        BString cleanNumberOnly = fData.channelLabel;
        int32 spaceIndex = cleanNumberOnly.FindFirst(" ");
        if (spaceIndex != B_ERROR) { cleanNumberOnly.Truncate(spaceIndex); }
        cleanNumberOnly.Trim();
        std::string targetChannel = cleanNumberOnly.String();

        time_t rawToday = time(nullptr);
        struct tm* localToday = localtime(&rawToday);
        
        std::string currentViewDateStr = "";
        std::string currentViewTimeStr = "";
        
        if (owner != nullptr && owner->Window() != nullptr) {
            BTextControl* dateInput = dynamic_cast<BTextControl*>(owner->Window()->FindView("date_input")); 
            BTextControl* timeInput = dynamic_cast<BTextControl*>(owner->Window()->FindView("time_input")); 
            if (dateInput != nullptr && dateInput->Text() != nullptr) {
                currentViewDateStr = dateInput->Text();
            }
            if (timeInput != nullptr && timeInput->Text() != nullptr) {
                currentViewTimeStr = timeInput->Text();
            }
        }

        // =========================================================================
        // COMBINED SINGLE PASS RENDER ENGINE (DYNAMIC SPAN & GLOW INTEGRATION)
        // =========================================================================
        float currentLeft = itemRect.left + kHeaderWidth + 1.0;
        
        gScheduleLocker.Lock();
        for (size_t idx = 0; idx < fData.programs.size(); idx++) {
            auto prog = fData.programs[idx];

            // A. Check if this program started in a previous column block step
            bool isContinuation = false;
            for (int32 prev = (int32)idx - 1; prev >= 0; prev--) {
                if (fData.programs[prev].title == prog.title) {
                    isContinuation = true;
                    break;
                }
            }

            float standardStepWidth = kCellWidth; 
            
            // B. Skip calculation loops if a previous wide block handles this show
            if (isContinuation) {
                currentLeft += standardStepWidth;
                continue; 
            }

            // C. Determine how many 30-minute block slots this program spans
            int32 cellsToSpan = 1;
            for (size_t next = idx + 1; next < fData.programs.size(); next++) {
                if (fData.programs[next].title == prog.title) {
                    cellsToSpan++;
                } else {
                    break;
                }
            }

            // D. Compute the expanded horizontal boundaries for wide program cards
            float spannedCellWidth = (cellsToSpan * standardStepWidth) - 12.0f;
            BRect cellRect(currentLeft, itemRect.top + 12, currentLeft + spannedCellWidth, itemRect.bottom - 13);

            // E. Clean formatting to match native Haiku interface layouts
            BString displayTimeText = prog.timeDisplay;
            if (displayTimeText == "LIVE NOW") {
                int32 h = localToday->tm_hour;
                int32 m = localToday->tm_min;
                
                if (!currentViewTimeStr.empty()) {
                    sscanf(currentViewTimeStr.c_str(), "%d:%d", &h, &m);
                }
                
                m += (int32)(idx * 30);
                h += m / 60;   m = m % 60;   h = h % 24;
                
                char calculatedTimeBuf[32];
                snprintf(calculatedTimeBuf, sizeof(calculatedTimeBuf), "%d:%02d %s", 
                         (h > 12 ? h - 12 : (h == 0 ? 12 : h)), m, (h >= 12 ? "PM" : "AM"));
                displayTimeText = calculatedTimeBuf;
            }

            std::string normalizedCellTime = "";
            int hours = 0, minutes = 0;
            char ampm[16] = {0};
            
            if (sscanf(displayTimeText.String(), "%d:%d %15s", &hours, &minutes, ampm) >= 2) {
                std::string ampmStr(ampm);
                if (ampmStr.find("PM") != std::string::npos || ampmStr.find("pm") != std::string::npos) {
                    if (hours < 12) hours += 12;
                } else if (ampmStr.find("AM") != std::string::npos || ampmStr.find("am") != std::string::npos) {
                    if (hours == 12) hours = 0;
                }
                char normBuffer[32];
                snprintf(normBuffer, sizeof(normBuffer), "%02d:%02d", hours, minutes);
                normalizedCellTime = normBuffer;
            } else {
                normalizedCellTime = displayTimeText.String(); 
            }


            // =========================================================================
            // F. Compare entries with the active recording scheduling queue (DIRECT FIX)
            // =========================================================================
            bool isScheduled = false;

            std::tm cellTm = {0}; 
            cellTm.tm_sec   = 0;
            cellTm.tm_isdst = -1; 

            BString activeSelectedDateStr = "";
            if (owner != nullptr) {
                activeSelectedDateStr = currentViewDateStr.c_str(); 
            }

            // Fallback: If currentViewDateStr was empty, look through the database queue matches
            if (activeSelectedDateStr.IsEmpty()) {
                for (size_t i = 0; i < gScheduleList.size(); i++) {
                   if (gScheduleList[i].channel == targetChannel && gScheduleList[i].showTitle == prog.title.String()) {
                        activeSelectedDateStr = gScheduleList[i].startDate.c_str();
                        break;
                    }
                }
            }

            // Last resort safety fallback: default to the first available schedule
            if (activeSelectedDateStr.IsEmpty() && !gScheduleList.empty()) {
                activeSelectedDateStr = gScheduleList[0].startDate.c_str();
            }

            int vy = 2026, vm = 6, vd = 25;
            if (!activeSelectedDateStr.IsEmpty()) {
                std::sscanf(activeSelectedDateStr.String(), "%d-%d-%d", &vy, &vm, &vd);
            }
            
            cellTm.tm_year = vy - 1900;
            cellTm.tm_mon  = vm - 1;
            cellTm.tm_mday = vd;
            cellTm.tm_hour = hours;
            cellTm.tm_min  = minutes;

            // =========================================================================
            //  MATRIX DRAWER DATE ROLLOVER ENGINE
            // =========================================================================
            // Read the main view's base hour control value to evaluate late night grids
            int baseWindowHour = 12;
            if (owner != nullptr && owner->Window() != nullptr) { // Use owner->Window() instead of Window()
                BTextControl* timeInput = dynamic_cast<BTextControl*>(owner->Window()->FindView("time_input"));
                if (timeInput != nullptr && timeInput->Text() != nullptr) {
                    int parsedH = 12, parsedM = 0;
                    BString rawT(timeInput->Text());
                    if (std::sscanf(rawT.String(), "%d:%d", &parsedH, &parsedM) >= 1) {
                        if (rawT.IFindFirst("PM") != B_ERROR && parsedH < 12) parsedH += 12;
                        baseWindowHour = parsedH;
                    }
                }
            }

            // If the view context is late evening (10 PM or 11 PM) 
            // but this specific grid cell falls in the morning (12 AM - 4:59 AM)
            if (baseWindowHour >= 22 && hours >= 0 && hours < 5) {
                cellTm.tm_mday += 1; // Advance timeline cell cleanly to tomorrow's date!
            }
            // =========================================================================

            
            time_t cellEpochTime = std::mktime(&cellTm);

            // Loop through schedules
            for (size_t i = 0; i < gScheduleList.size(); i++) {
                if (!gScheduleList[i].processed) {
                    // Check exact channel match ("11.1" == "11.1")
                    bool channelMatch = (gScheduleList[i].channel == targetChannel);
                    
                    // Validate the program text name matches the scheduled title 
                    // This prevents long recordings from bleeding into different television shows!
                    bool titleMatch = (gScheduleList[i].showTitle == prog.title.String());
                    
                    time_t recordStart = gScheduleList[i].epochStart;
                    time_t recordEnd   = recordStart + gScheduleList[i].durationSec;
                    bool timeMatch = (cellEpochTime >= recordStart && cellEpochTime < recordEnd);

                    // =========================================================================
                    // TERMINAL DIAGNOSTIC VERIFICATION PRINT LAYER
                    // =========================================================================
                    if (channelMatch && titleMatch && cfg.debugEnable) {
                        std::tm* safeCheckTm = std::localtime(&cellEpochTime);
                        std::cout << "\n[GUIDE MATCH DEBUG] Target Channel: " << targetChannel << "\n"
                                  << "  -> Grid View Date Structure: " << (safeCheckTm->tm_year + 1900) << "-" << (safeCheckTm->tm_mon + 1) << "-" << safeCheckTm->tm_mday << " @ " << hours << ":" << (minutes < 10 ? "0" : "") << minutes << "\n"
                                  << "  -> Computed Cell Epoch: " << cellEpochTime << "\n"
                                  << "  -> Schedule Item Title: " << gScheduleList[i].showTitle << "\n"
                                  << "  -> Schedule Date/Time:  " << gScheduleList[i].startDate << " @ " << gScheduleList[i].startTime << "\n"
                                  << "  -> Record Window Epoch: " << recordStart << " to " << recordEnd << "\n"
                                  << "  -> Time Evaluated Match: " << (timeMatch ? "TRUE (RED)" : "FALSE") << "\n"
                                  << "  -> Math Delta (Cell - RecordStart): " << (cellEpochTime - recordStart) << " seconds\n"
                                  << "------------------------------------------------------------" << std::endl;
                    }
                    // =========================================================================


                    // Requires all three parameters to align perfectly
                    if (channelMatch && titleMatch && timeMatch) {
                        isScheduled = true;
                        break;
                    }
                }
            }


            // =========================================================================
            // G. DRAW MATRIX CARD BACKGROUND SHAPES & INTERACTIVE HOVER GLOWS
            // =========================================================================
            rgb_color normalCellBg    = { 26, 26, 26, 255 };      
            rgb_color standardBorder  = { 45, 45, 45, 255 };      
            rgb_color scheduledBgColor = { 75, 20, 20, 255 };     
            rgb_color borderRed        = { 220, 40, 40, 255 };                
            rgb_color glowBorderColor = { 0, 210, 210, 255 };     
            rgb_color glowInnerColor  = { 12, 45, 45, 255 };      

            // ISOLATION BLOCK 1: Card Shapes Canvas
            owner->PushState();
            if (isScheduled) {
                owner->SetHighColor(scheduledBgColor);
                owner->FillRect(cellRect);
                owner->SetLowColor(scheduledBgColor);
            } else {
                owner->SetHighColor(((int32)idx == fHoveredCellIndex) ? glowInnerColor : normalCellBg);
                owner->FillRect(cellRect);
                owner->SetLowColor(((int32)idx == fHoveredCellIndex) ? glowInnerColor : normalCellBg);
            }
            owner->PopState();

            // ISOLATION BLOCK 2: Vector Framing Boundaries
            owner->PushState();
            if (isScheduled) {
                owner->SetHighColor(((int32)idx == fHoveredCellIndex) ? glowBorderColor : borderRed);
                owner->StrokeRect(cellRect);
                owner->StrokeRect(cellRect.InsetByCopy(1.0, 1.0)); 
            } else {
                owner->SetHighColor(((int32)idx == fHoveredCellIndex) ? glowBorderColor : standardBorder);
                owner->StrokeRect(cellRect);
                if ((int32)idx == fHoveredCellIndex) {
                    owner->StrokeRect(cellRect.InsetByCopy(1.0, 1.0));
                }
            }
            owner->PopState();

  			// =========================================================================
            // H. FIELD STRINGS PLACEMENTS (FIXED VIEWPORT SHIFT ANCHORING)
            // =========================================================================
            BString timeRangeStr;
            
            // 1. Parse the unchanging structural end time from your struct (e.g., "11:00 PM")
            int32 endHour = 0;
            int32 endMinute = 0;
            std::memset(ampm, 0, sizeof(ampm));
            
            if (sscanf(prog.endTimeStr.String(), "%d:%d %15s", &endHour, &endMinute, ampm) >= 2) {
                BString ampmStr(ampm);
                if (ampmStr.IFindFirst("PM") != B_ERROR && endHour < 12) {
                    endHour += 12;
                } else if (ampmStr.IFindFirst("AM") != B_ERROR && endHour == 12) {
                    endHour = 0;
                }
            } else {
                sscanf(prog.endTimeStr.String(), "%d:%d", &endHour, &endMinute);
            }

            // 2. Fetch the absolute true total duration directly from your structural member
            int32 totalDurationMinutes = prog.durationMinutes; 
            if (totalDurationMinutes <= 0) {
                totalDurationMinutes = 30; // Safety fallback
            }

            // 3. Work BACKWARDS from the absolute end time to calculate the true immutable start time
            int32 endTotalMinutes = (endHour * 60) + endMinute;
            int32 startTotalMinutes = endTotalMinutes - totalDurationMinutes;
            
            // Track if we crossed midnight to sync with your calendar advance logic
            bool advanceCalendarDayFlag = false;
            if (startTotalMinutes < 0) {
                startTotalMinutes += (24 * 60); // Handle midnight wrapping
                advanceCalendarDayFlag = true;
            }
            
            // 4. Convert back to clean 12-hour AM/PM parameters for display
            int32 trueStartHour = (startTotalMinutes / 60) % 24;
            int32 trueStartMin = startTotalMinutes % 60;
            const char* startPeriod = (trueStartHour >= 12) ? "PM" : "AM";
            int32 displayStartHour = (trueStartHour > 12) ? (trueStartHour - 12) : ((trueStartHour == 0) ? 12 : trueStartHour);

            int32 displayEndHour = (endHour > 12) ? (endHour - 12) : ((endHour == 0) ? 12 : endHour);
            const char* endPeriod = (endHour >= 12) ? "PM" : "AM";

            // DYNAMIC DATE LINK ENGINE: MATCHED SYSTEM REGISTERED NAME
            BString finalizedRecordDate = "";

            if (owner != nullptr && owner->Window() != nullptr) {
                // MATCH EXACT CONSTRUCTOR ID: "timelineHeader" instead of "TimelineHeaderView"
                BView* baseHeader = owner->Window()->FindView("timelineHeader");
                
                // Fallback sibling lookup tree search if layout is nested inside container shelves
                if (baseHeader == nullptr && owner->Parent() != nullptr) {
                    baseHeader = owner->Parent()->FindView("timelineHeader");
                }

                if (baseHeader != nullptr) {
                    TimelineHeaderView* headerView = dynamic_cast<TimelineHeaderView*>(baseHeader);
                    if (headerView != nullptr && !headerView->fCachedSelectedDate.IsEmpty()) {
                        finalizedRecordDate = headerView->fCachedSelectedDate;
                    }
                }
            }

            // Fall back cleanly to real-world system clock if selected string is empty
            if (finalizedRecordDate.IsEmpty()) {
                std::time_t currentSystemEpoch = std::time(nullptr);
                std::tm* currentSystemTm = std::localtime(&currentSystemEpoch);
                char autoSysBuf[32];
                std::strftime(autoSysBuf, sizeof(autoSysBuf), "%Y-%m-%d", currentSystemTm);
                finalizedRecordDate = autoSysBuf;
            }
            
            if (advanceCalendarDayFlag) {
                int parsedYear = 2026, parsedMonth = 6, parsedDay = 25;
                // Dynamically read whatever date we resolved above rather than hardcoding 2026-06-25
                if (std::sscanf(finalizedRecordDate.String(), "%d-%d-%d", &parsedYear, &parsedMonth, &parsedDay) == 3) {
                    std::tm rolloverTimeBox = {0};
                    rolloverTimeBox.tm_year = parsedYear - 1900;
                    rolloverTimeBox.tm_mon  = parsedMonth - 1;
                    rolloverTimeBox.tm_mday = parsedDay + 1; // Bump calendar day forward explicitly
                    rolloverTimeBox.tm_hour = 12;
                    rolloverTimeBox.tm_isdst = -1;
                    
                    std::time_t normalizedFutureEpoch = std::mktime(&rolloverTimeBox);
                    if (normalizedFutureEpoch != (std::time_t)-1) {
                        std::tm* safeFutureTm = std::localtime(&normalizedFutureEpoch);
                        char rebalancedDateBuf[32]; 
                        std::strftime(rebalancedDateBuf, sizeof(rebalancedDateBuf), "%Y-%m-%d", safeFutureTm);
                        finalizedRecordDate = rebalancedDateBuf;
                    }
                }
            }

            // 5. Construct the range string with the matching date string format
            timeRangeStr.SetToFormat("%d:%02d %s - %d:%02d %s (%s)", 
                displayStartHour, trueStartMin, startPeriod,
                displayEndHour, endMinute, endPeriod,
                finalizedRecordDate.String()
            );

            // Maintain sliding pen bounds tracking so the text stays on screen when sliding left
            float visibleLeftX = cellRect.left + 20;
            if (visibleLeftX < (itemRect.left + kHeaderWidth + 20)) {
                visibleLeftX = itemRect.left + kHeaderWidth + 20;
            }

            // ISOLATION BLOCK 3: Timeline Metadata Text
            owner->PushState();
            BFont timeFont;
            owner->GetFont(&timeFont);
            timeFont.SetFace(B_REGULAR_FACE);
            timeFont.SetSize(10.0); 
            owner->SetFont(&timeFont);
            owner->SetHighColor(isScheduled ? rgb_color{255, 140, 140, 255} : rgb_color{140, 140, 140, 255});
            
            owner->MovePenTo(visibleLeftX, cellRect.top + 28);
            owner->DrawString(timeRangeStr.String()); 
            owner->PopState();

            // ISOLATION BLOCK 4: Program Header Title
            owner->PushState();
            BFont titleFont;
            owner->GetFont(&titleFont);
            titleFont.SetFace(B_BOLD_FACE);
            titleFont.SetSize(12.0); 
            owner->SetFont(&titleFont);
            owner->SetHighColor(isScheduled ? rgb_color{255, 255, 255, 255} : textColor);         
            owner->MovePenTo(cellRect.left + 20, cellRect.top + 50);
            BString truncatedTitle = prog.title;
            owner->TruncateString(&truncatedTitle, B_TRUNCATE_END, cellRect.Width() - 32);
            owner->DrawString(truncatedTitle.String());
            owner->PopState();

            // ISOLATION BLOCK 5: Block Summary Narrative Text
            owner->PushState();
            BFont descFont;
            owner->GetFont(&descFont);
            descFont.SetFace(B_ITALIC_FACE);
            descFont.SetSize(11.0); 
            owner->SetFont(&descFont);
            owner->SetHighColor(isScheduled ? rgb_color{220, 200, 200, 255} : rgb_color{170, 170, 170, 255});
            owner->MovePenTo(cellRect.left + 20, cellRect.top + 72);
            BString shortDesc = prog.description;
            if (shortDesc.IsEmpty()) { 
                shortDesc = "No further program description text details provided by broadcaster."; 
            }
            owner->TruncateString(&shortDesc, B_TRUNCATE_END, cellRect.Width() - 32);
            owner->DrawString(shortDesc.String());
            owner->PopState();

            // =========================================================================
            // I. EXTRA LAYER FLAGS (RECORD BADGES ACCENT)
            // =========================================================================

            if (idx < 4) {
                fCellIsScheduledMap[idx] = isScheduled;
            }
            
            if (isScheduled) {
                owner->PushState(); 
                owner->SetHighColor(255, 60, 60, 255);
                BFont badgeFont;
                owner->GetFont(&badgeFont);
                badgeFont.SetFace(B_BOLD_FACE);
                badgeFont.SetSize(11.0);
                owner->SetFont(&badgeFont);
                
                float badgeX = cellRect.right - owner->StringWidth("[REC]") - 20;
                float badgeY = cellRect.top + 28; 
                owner->MovePenTo(badgeX, badgeY);
                owner->DrawString("[REC]");
                owner->PopState(); 
            }

            currentLeft += standardStepWidth;
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
        SetFlags(Flags() | B_POINTER_EVENTS);
    }

    void MouseMoved(BPoint point, uint32 transit, const BMessage* message) override {
        BListView::MouseMoved(point, transit, message);
        
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

                    BMessage playMsg(MSG_PLAY_IN_MPV);
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
                    
                    const float kHeaderWidth = 300.0;
                    const float kCellWidth = 350.0;
                    float localX = point.x - itemFrame.left;
                    
                    int32 cellIndex = -1;
                    if (localX > kHeaderWidth) {
                        cellIndex = (int32)((localX - kHeaderWidth) / kCellWidth);
                    }

                    BPopUpMenu* contextMenu = new BPopUpMenu("Context", false, false);
                    
                    BString watchLabel;
                    watchLabel << "Watch " << cleanNumberOnly.String() << " Live";
                    BMenuItem* playItem = new BMenuItem(watchLabel.String(), NULL);
                    contextMenu->AddItem(playItem);
                    
                    BMenuItem* queueItem = nullptr;
                    BMenuItem* removeItem = nullptr;
                    int32 matchingActiveIndex = -1;

                   if (cellIndex >= 0 && cellIndex < (int32)item->fData.programs.size()) {
                        std::string targetChannel = cleanNumberOnly.String(); 
                        std::string targetTitle = item->fData.programs[cellIndex].title.String();

                        // =========================================================================
                        // READ THE RED RENDERING FLAG DIRECTLY (NO MORE MATH DRIFT)
                        // =========================================================================
                        bool cellIsRedOnScreen = false;
                        if (cellIndex < 4) {
                            cellIsRedOnScreen = item->fCellIsScheduledMap[cellIndex];
                        }

                        if (cellIsRedOnScreen) {
                            // Find the active schedule list entry matching this show name and channel
                            gScheduleLocker.Lock();
                            int32 activeCounter = 0;
                            for (size_t i = 0; i < gScheduleList.size(); i++) {
                                if (!gScheduleList[i].processed) {
                                    bool channelMatch = (gScheduleList[i].channel == targetChannel);
                                    bool titleMatch   = (gScheduleList[i].showTitle == targetTitle);

                                    if (channelMatch && titleMatch) { 
                                        matchingActiveIndex = activeCounter;
                                        break;
                                    }
                                    activeCounter++;
                                }
                            }
                            gScheduleLocker.Unlock();
                        }

                        if (matchingActiveIndex != -1) {
                            BMessage* delMsg = new BMessage(MSG_REMOVE_SCHEDULE);
                            delMsg->AddInt32("list_index", matchingActiveIndex);
                            
                            removeItem = new BMenuItem("Remove Queue", delMsg);
                            contextMenu->AddItem(removeItem);
                        } else {
                            const auto& targetProgramBlock = item->fData.programs[cellIndex];
                            
                            BMessage* queueMsg = new BMessage(MSG_PREFILL_RECORD_SCHEDULE);
                            queueMsg->AddString("show_title", targetProgramBlock.title.String());
                            
                            // FIXED KEY NAME ALIGNMENT TO SYNC WITH THE AUTOMATED ENGINE
                            queueMsg->AddString("show_description", targetProgramBlock.description.String());
                            
                            queueMsg->AddString("start_time", targetProgramBlock.timeDisplay.String());
                            queueMsg->AddString("numeric_subchannel", cleanNumberOnly.String());
                            queueMsg->AddInt32("duration_minutes", targetProgramBlock.durationMinutes);
                            queueMsg->AddBool("auto_commit_queue", true); 

                            bool advanceDay = (cellIndex > 0 && 
                                               targetProgramBlock.timeDisplay.IFindFirst("AM") != B_ERROR && 
                                               item->fData.programs[0].timeDisplay.IFindFirst("PM") != B_ERROR);
                            
                            queueMsg->AddBool("advance_calendar_day", advanceDay);

                            queueItem = new BMenuItem("Add to Queue", queueMsg);
                            contextMenu->AddItem(queueItem);
                        }



                        contextMenu->AddSeparatorItem();
                        BMenuItem* viewRecsItem = new BMenuItem("Open Recordings", new BMessage(MSG_VIEW_RECORDINGS));
                        contextMenu->AddItem(viewRecsItem);

                        BMenuItem* showSchedItem = new BMenuItem("Open Scheduler", new BMessage(MSG_SHOW_MAIN_SCHEDULER));
                        contextMenu->AddItem(showSchedItem);
                       
                        BMenuItem* contextFsItem = new BMenuItem("Fullscreen Mode", new BMessage(MSG_GUIDE_TOGGLE_FULLSCREEN));
                        contextFsItem->SetMarked(cfg.fullscreenEnable);
                        contextMenu->AddItem(contextFsItem);               
						
                        BMenuItem* exitFsItem = new BMenuItem("Close Guide", new BMessage(MSG_CLOSE_GUIDE_WINDOW));
                        contextMenu->AddItem(exitFsItem);

                        BMenuItem* quitAppItem = new BMenuItem("Quit HaikuDVR", new BMessage(MSG_QUIT_ENTIRE_APP));
                        contextMenu->AddItem(quitAppItem);
                    }

                    BPoint screenPoint = ConvertToScreen(point) + BPoint(2, 2);
                    BMenuItem* selectedItem = contextMenu->Go(screenPoint, false, true, false);
                    
                    if (selectedItem != nullptr && fParentShortcutTarget != nullptr) {
                        if (selectedItem == playItem) {
                            BMessage playMsg(MSG_PLAY_IN_MPV);
                            playMsg.AddString("numeric_channel", cleanNumberOnly.String());
                            fParentShortcutTarget->PostMessage(&playMsg);
                        } 
                        else if (queueItem != nullptr && selectedItem == queueItem) {
                            // =========================================================================
                            // DYNAMIC ROOT CELL LOOKUP FOR RECORD SCHEDULING (CONTINUATION AWARE)
                            // =========================================================================
                            int32 rootCellIndex = cellIndex;
                            
                            if (rootCellIndex > 0 && rootCellIndex < (int32)item->fData.programs.size()) {
                                std::string currentTitle = item->fData.programs[rootCellIndex].title.String();
                                for (int32 prev = rootCellIndex - 1; prev >= 0; prev--) {
                                    if (item->fData.programs[prev].title.String() == currentTitle) {
                                        rootCellIndex = prev; 
                                    } else {
                                        break; 
                                    }
                                }
                            }

                            const auto& targetProg = item->fData.programs[rootCellIndex];

                            BMessage selectionBroadcast(MSG_PREFILL_RECORD_SCHEDULE);
                            selectionBroadcast.AddString("show_title", targetProg.title.String());                            
                            selectionBroadcast.AddString("show_description", targetProg.description.String());                            
                            selectionBroadcast.AddString("channel_label", item->fData.channelLabel.String());
                            selectionBroadcast.AddInt32("duration_minutes", targetProg.durationMinutes);

                            
                            BString targetSubchannel = item->fData.channelLabel;
                            int32 spaceIndex = targetSubchannel.FindFirst(" ");
                            if (spaceIndex != B_ERROR) {
                                targetSubchannel.Truncate(spaceIndex);
                            }
                            targetSubchannel.Trim();
                            selectionBroadcast.AddString("numeric_subchannel", targetSubchannel.String());
                            
                            BString targetTimeStr = targetProg.timeDisplay;
 
                            if (targetTimeStr == "LIVE NOW") {
                                time_t now = real_time_clock();
                                struct tm* timeInfo = localtime(&now);
                                
                                char todayBuffer[32];
                                strftime(todayBuffer, sizeof(todayBuffer), "%Y-%m-%d", timeInfo);
                                std::string todayStr(todayBuffer);
                                std::string currentViewDateStr = todayStr;
                                std::string currentViewTimeStr = "15:00";
                                
                                if (Window() != nullptr) {
                                    BTextControl* dateInput = dynamic_cast<BTextControl*>(Window()->FindView("date_input"));
                                    BTextControl* timeInput = dynamic_cast<BTextControl*>(Window()->FindView("time_input"));
                                    if (dateInput != nullptr && dateInput->Text() != nullptr) currentViewDateStr = dateInput->Text();
                                    if (timeInput != nullptr && timeInput->Text() != nullptr) currentViewTimeStr = timeInput->Text();
                                }
                                
                                if (currentViewDateStr != todayStr) {
                                    int h = 12, m = 0;
                                    if (sscanf(currentViewTimeStr.c_str(), "%d:%d", &h, &m) >= 1) {
                                        m += (int)(rootCellIndex * 30);
                                        h += m / 60; m = m % 60; h = h % 24;
                                        char adjustedBuffer[32];
                                        snprintf(adjustedBuffer, sizeof(adjustedBuffer), "%d:%02d %s", 
                                                 (h > 12 ? h - 12 : (h == 0 ? 12 : h)), m, (h >= 12 ? "PM" : "AM"));
                                        targetTimeStr = adjustedBuffer;
                                    }
                                } else {
                                    char adjustedBuffer[32];
                                    snprintf(adjustedBuffer, sizeof(adjustedBuffer), "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
                                    targetTimeStr = adjustedBuffer;
                                }
                            }
                            else if (targetTimeStr == "NEXT" || targetTimeStr == "LATER") {
                                time_t now = real_time_clock();
                                int32 shiftSeconds = (targetTimeStr == "NEXT") ? (30 * 60) : (60 * 60);
                                time_t blockTime = now + shiftSeconds;
                                struct tm* blockInfo = localtime(&blockTime);
                                char adjustedBuffer[32];
                                snprintf(adjustedBuffer, sizeof(adjustedBuffer), "%02d:%02d", blockInfo->tm_hour, blockInfo->tm_min);
                                targetTimeStr = adjustedBuffer;
                            }

                            // =========================================================================
                            //  STRUCTURAL FIX: STRING BUFFER STRING CORRECTION (COMPILATION REBALANCED)
                            // =========================================================================
                            int32 finalRecordYear = 2026;
                            int32 finalRecordMonth = 6;
                            int32 finalRecordDay = 25;
                            bool automaticallyAdvanceRecordDate = false;

                            // 1. Extract the active selected date directly from the guide interface view
                            if (Window() != nullptr) {
                                BTextControl* dateInput = dynamic_cast<BTextControl*>(Window()->FindView("date_input"));
                                if (dateInput != nullptr && dateInput->Text() != nullptr) {
                                    std::sscanf(dateInput->Text(), "%d-%d-%d", &finalRecordYear, &finalRecordMonth, &finalRecordDay);
                                }
                            }

                            // 2. Extract and evaluate the program's literal start time characters
                            BString internalTimeRaw = targetProg.timeDisplay;
                            int32 parsedProgHour = 12;
                            int32 parsedProgMin = 0;
                            char parsedAmPm[16] = {0};

                            if (std::sscanf(internalTimeRaw.String(), "%d:%d %15s", &parsedProgHour, &parsedProgMin, parsedAmPm) >= 2) {
                                BString ampmCheck(parsedAmPm);
                                if (ampmCheck.IFindFirst("AM") != B_ERROR && parsedProgHour == 12) {
                                    parsedProgHour = 0;
                                }
                                if (ampmCheck.IFindFirst("PM") != B_ERROR && parsedProgHour < 12) {
                                    parsedProgHour += 12;
                                }
                            } else {
                                std::sscanf(internalTimeRaw.String(), "%d:%d", &parsedProgHour, &parsedProgMin);
                            }

                            // 3. Roll day context forward if program starts during early morning grid zones
                            if (parsedProgHour >= 0 && parsedProgHour < 5) {
                                automaticallyAdvanceRecordDate = true;
                                
                                std::tm rolloverBox = {0};
                                rolloverBox.tm_year = finalRecordYear - 1900;
                                rolloverBox.tm_mon  = finalRecordMonth - 1;
                                rolloverBox.tm_mday = finalRecordDay + 1; // Step forward 1 day
                                rolloverBox.tm_hour = 12;
                                rolloverBox.tm_isdst = -1;
                                
                                time_t normalizedEpoch = std::mktime(&rolloverBox);
                                if (normalizedEpoch != (time_t)-1) {
                                    struct tm* safeTm = localtime(&normalizedEpoch);
                                    finalRecordYear = safeTm->tm_year + 1900;
                                    finalRecordMonth = safeTm->tm_mon + 1;
                                    finalRecordDay = safeTm->tm_mday;
                                }
                            }

                            // 4. Print clean characters into safe buffer strings
                            char localizedDateBuf[32] = {0};
                            std::snprintf(localizedDateBuf, sizeof(localizedDateBuf), "%04d-%02d-%02d", 
                                          finalRecordYear, finalRecordMonth, finalRecordDay);

                            selectionBroadcast.AddString("start_time", targetTimeStr.String());
                            
                            // FIXED: Explicitly transmit the string to sync your scheduler window
                            selectionBroadcast.AddString("start_date", localizedDateBuf);
                            selectionBroadcast.AddString("date_string", localizedDateBuf); // Backup match target
                            
                            selectionBroadcast.AddBool("auto_commit_queue", true);
                            selectionBroadcast.AddBool("advance_calendar_day", automaticallyAdvanceRecordDate);
                            
                            fParentShortcutTarget->PostMessage(&selectionBroadcast);
                        }
                        else if (removeItem != nullptr && selectedItem == removeItem) {
                            BMessage removeMsg(MSG_REMOVE_SCHEDULE);
                            removeMsg.AddInt32("list_index", matchingActiveIndex);
                            fParentShortcutTarget->PostMessage(&removeMsg);
                        }
                        else if (selectedItem->Message() != nullptr && selectedItem->Message()->what == MSG_VIEW_RECORDINGS) {
                            fParentShortcutTarget->PostMessage(MSG_VIEW_RECORDINGS);
                        }
                        else if (selectedItem->Message() != nullptr && selectedItem->Message()->what == MSG_SHOW_MAIN_SCHEDULER) {
                            fParentShortcutTarget->PostMessage(MSG_SHOW_MAIN_SCHEDULER);
                        }
                        else if (selectedItem->Message() != nullptr && selectedItem->Message()->what == MSG_GUIDE_TOGGLE_FULLSCREEN) {
                            fParentShortcutTarget->PostMessage(MSG_GUIDE_TOGGLE_FULLSCREEN);
                        }
                        else if (selectedItem->Message() != nullptr && selectedItem->Message()->what == MSG_CLOSE_GUIDE_WINDOW) {
                            Window()->PostMessage(B_QUIT_REQUESTED);
                        }
                        else if (selectedItem->Message() != nullptr && selectedItem->Message()->what == MSG_QUIT_ENTIRE_APP) {
                            fParentShortcutTarget->PostMessage(MSG_QUIT_ENTIRE_APP);
                        }
                    }
                    delete contextMenu;
                }
            }
        }
    }
};


// =========================================================================
// 3. INTERACTIVE TV GUIDE MATRIX WINDOW (FULLSCREEN BY DEFAULT)
// =========================================================================
class RealTVGuideWindow : public BWindow {
public:
    RealTVGuideWindow(BRect frame, const std::vector<ChannelGuideItem>& loadedChannels, BListView* mainChannelListView, BWindow* mainAppWindow) 
        : BWindow(frame, "Interactive TV Guide Matrix", B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS) {

        if (cfg.fullscreenEnable) {
            BScreen screen(this);
            if (screen.IsValid()) {
                BRect screenFrame = screen.Frame();
                
                MoveTo(screenFrame.left, screenFrame.top);
                ResizeTo(screenFrame.Width(), screenFrame.Height());

                SetLook(B_NO_BORDER_WINDOW_LOOK);
                SetFlags(Flags() | B_NOT_MOVABLE | B_NOT_RESIZABLE);
            } else {
                ResizeTo(1050, 650);
            }
        } else {
            // Standard user interface window boundaries if fullscreen is disabled
            SetLook(B_DOCUMENT_WINDOW_LOOK);
            ResizeTo(1050, 650);
            
            // Re-center window safely on display relative to the main workspace layout frame
            CenterOnScreen();
        }
        
        fMainAppWindow = mainAppWindow;
        fMainChannelListView = mainChannelListView;
        
        TimelineHeaderView* headerTimelineBar = new TimelineHeaderView(BRect(0, 0, Bounds().Width(), 40), mainAppWindow);
        headerTimelineBar->SetExplicitMinSize(BSize(B_SIZE_UNLIMITED, 40));
        headerTimelineBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 40));

        fContainerList = new InteractiveGuideListView("guideListContainer", mainAppWindow);
        _BuildGuideRowsFromLiveChannels(loadedChannels, mainChannelListView);
        
		BScrollView* scrollWrapper = new BScrollView("guideScroll", fContainerList, 0, false, true);        
		AddShortcut(B_ESCAPE, 0, new BMessage(B_QUIT_REQUESTED));

        BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
            .SetInsets(0, 0, 0, 0)
            .Add(headerTimelineBar) 
            .Add(scrollWrapper)
        .End();
    }

	bool QuitRequested() override {
	    if (fMainAppWindow != nullptr) {
	        fMainAppWindow->PostMessage(MSG_GUIDE_CLOSED);
	    }
	    return true; 
	}
	
	void MessageReceived(BMessage* message) override;
    BWindow* GetMainWindow() const { return fMainAppWindow; }

private:    
    BWindow* fMainAppWindow;
public:
    BListView* fMainChannelListView;
    BListView* fContainerList;
    
	// @Datapusher
	void _BuildGuideRowsFromLiveChannels(const std::vector<ChannelGuideItem>& loadedChannels, BListView* mainListView) {
		    if (mainListView == nullptr) return;
		
		    // =========================================================================
		    // INLINE NARRATIVE SCRUBBER METHOD (XML/HTML ENTITY DECODER ENGINE)
		    // =========================================================================
		    auto SanitizeTextEngine = [](const char* rawInput) -> BString {
		        BString cleanedStr(rawInput);
		        if (cleanedStr.IsEmpty()) return cleanedStr;
		        
		        // Decode standard entity character masks back to pristine plain text
		        cleanedStr.ReplaceAll("&quot;", "\"");
		        cleanedStr.ReplaceAll("&amp;",  "&");
		        cleanedStr.ReplaceAll("&apos;", "'");
		        cleanedStr.ReplaceAll("&#39;",  "'"); // Resolves numeric single quotes
		        cleanedStr.ReplaceAll("&lt;",   "<");
		        cleanedStr.ReplaceAll("&gt;",   ">");
		        
		        // Strip accidental double-spacing fragments
		        cleanedStr.ReplaceAll("  ", " ");
		        return cleanedStr;
		    };
		    // =========================================================================
	
		    for (size_t i = 0; i < loadedChannels.size(); i++) {
		        const auto& liveChan = loadedChannels[i];
		        
		        ChannelListItem* channelItem = (ChannelListItem*)mainListView->ItemAt(i);
		        const BBitmap* associatedIcon = (channelItem != nullptr) ? channelItem->channelIcon : nullptr;
		        
		        GuideRowModel rowData;
		        rowData.channelLabel << liveChan.guideNumber.c_str() << " - " << liveChan.guideName.c_str();
		        rowData.channelIcon = associatedIcon;
		        
		        BString finalColumn1Time = "7:00 PM";
		
		        if (!liveChan.futureLineup.empty()) {
		            BString column2Time = liveChan.futureLineup[0].startTimeStr.c_str();
		            
		            int h = 0, m = 0;
		            char ampm[16] = {0};
		
		            bool parsed = false;
		            if (sscanf(column2Time.String(), "%d:%d %15s", &h, &m, ampm) >= 2) {
		                parsed = true;
		            } else if (sscanf(column2Time.String(), "%d:%d%15s", &h, &m, ampm) >= 2) {
		                parsed = true;
		            } else if (sscanf(column2Time.String(), "%d:%d", &h, &m) == 2) {
		                strcpy(ampm, (h >= 12) ? "PM" : "AM");
		                if (h > 12) h -= 12;
		                if (h == 0) h = 12;
		                parsed = true;
		            }
		
		            if (parsed) {
		                std::string ampmStr(ampm);
		                int32 militaryHour = h;
		                
		                if ((ampmStr.find("PM") != std::string::npos || ampmStr.find("pm") != std::string::npos) && h < 12) {
		                    militaryHour += 12;
		                } else if ((ampmStr.find("AM") != std::string::npos || ampmStr.find("am") != std::string::npos) && h == 12) {
		                    militaryHour = 0;
		                }
		
		                int32 totalMinutes = (militaryHour * 60) + m - 30;
		                if (totalMinutes < 0) {
		                    totalMinutes += (24 * 60);
		                }
		
		                int32 calculatedHour = totalMinutes / 60;
		                int32 calculatedMin = totalMinutes % 60;
		                const char* periodMarker = (calculatedHour >= 12) ? "PM" : "AM";
		                
		                int32 displayHour = (calculatedHour > 12) ? (calculatedHour - 12) : ((calculatedHour == 0) ? 12 : calculatedHour);
		                
		                finalColumn1Time.SetToFormat("%d:%02d %s", displayHour, calculatedMin, periodMarker);
		            }
		        }
		
		        // Push the sanitized Now Playing row metadata elements
		        rowData.programs.push_back({ 
		            SanitizeTextEngine(liveChan.nowPlaying.c_str()).String(), // Sanitized Title
		            finalColumn1Time.String(), 
		            350.0f, 
		            liveChan.nowPlayingDurationMinutes,
		            liveChan.nowPlayingEndTimeStr.c_str(),  
		            SanitizeTextEngine(liveChan.nowPlayingDescription.c_str()).String() // Sanitized Description
		            
		        });
		        
		        for (const auto& nextShow : liveChan.futureLineup) {
		            BString nextTimeLabel = nextShow.startTimeStr.c_str();
		            
		            int hNext = 0, mNext = 0;
		            if (nextTimeLabel.FindFirst("PM") == B_ERROR && nextTimeLabel.FindFirst("AM") == B_ERROR) {
		                if (sscanf(nextTimeLabel.String(), "%d:%d", &hNext, &mNext) == 2) {
		                    nextTimeLabel.SetToFormat("%d:%02d %s", (hNext > 12 ? hNext - 12 : (hNext == 0 ? 12 : hNext)), mNext, (hNext >= 12 ? "PM" : "AM"));
		                }
		            }
		
		            // Push the sanitized Future Lineup row metadata elements
		            rowData.programs.push_back({
		                SanitizeTextEngine(nextShow.title.c_str()).String(), // Sanitized Title
		                nextTimeLabel.String(), 
		                350.0f, 
		                nextShow.durationMinutes,
		                nextShow.endTimeStr.String(),     
		                SanitizeTextEngine(nextShow.description.String()).String() // Sanitized Description
		                
		            });
		        }
		        
		        fContainerList->AddItem(new GuideListRowItem(rowData, i));
		    }
		}

};




class DVRWindow : public BWindow {

private:					
	enum ChannelFilter {
	    FILTER_ALL,
	    FILTER_HD,
	    FILTER_SD
	};
    std::map<std::string, ChannelGuideItem> cloudGuideMap;
    std::map<std::string, std::string> xmlIdToChannelNumMap; 
    std::map<std::string, std::string> cloudIconMap;
	std::string fCachedGuidePayload; 
	BMenuItem *fPlayerMpvItem, *fPlayerMediaItem, *fPlayerHtvItem, *fPlayerVlcItem;
   	BWindow* fRecordingsBrowser = nullptr;
	BWindow* fGuideWindow = nullptr; 
    std::vector<BBitmap*> fIconCache;
	BStringView*  fBackendStatusLabel;
	BButton*      fRestartBackendButton;
	BButton* 	  fDateBrowseButton;     
	int32 		  fCountdownSecondsRemaining; 
	BStringView*  fCountdownLabel;     
	std::string   fSelectedDirectory;
	BFilePanel*   fFolderPanel;      
	BStringView*  fPathDisplayLabel; 
	BButton* 	  fBrowseButton;       
	BMenuItem* 	  fNotifyOnItem;
	BMenuItem* 	  fNotifyOffItem;
	BMenuItem* 	  fDlnaOnItem;
	BMenuItem* 	  fDlnaOffItem;
	BMenuItem*    fAboutItem;
	BMenuItem* 	  fDebugOnItem;
	BMenuItem* 	  fDebugOffItem;
	BMenuItem* 	  fFullscreenOnItem;
	BMenuItem* 	  fFullscreenOffItem;
	BMessageRunner* fRefreshRunner;
	time_t   	  fLastNetworkSyncTime; 
	ChannelFilter fCurrentFilter;
	BTextControl* fDateInput; 
    BMenuField*   fTunerSelector;
    BPopUpMenu*   fTunerMenu;
    BTextControl* fChannelInput;
    BMenuField*   fDurationSelector; 
    BPopUpMenu*   fDurationMenu;
    BTextControl* fTimeInput; 
    BButton* 	  fRecordButton;
    BButton* 	  fStopButton;
    BButton* 	  fScheduleButton; 
    BStringView*  fStatusLabel;    
    BListView*    fChannelListView;
    BScrollView*  fChannelScrollView;
    std::vector<ChannelGuideItem> fLoadedChannels;    
    ScheduleListView* fScheduleListView;
    BScrollView*  fScheduleScrollView;
    BStringView*  fScheduleHeading;
    std::string   fSelectedIp;
    std::string   fSelectedDurationSeconds; 
    thread_id     fActiveThread;
    thread_id     fSchedulerThread;
    BMessenger*   fSchedulerMessenger;
    std::vector<std::string> DiscoverAllTuners() {
        std::vector<std::string> tuners;
        struct hdhomerun_discover_device_t result_list[64];
        hdhomerun_discover_t* ds = hdhomerun_discover_create(NULL);
        if (ds == NULL) {
            return tuners;
        }
        
        int count = hdhomerun_discover_find_devices_v2(ds, 0, HDHOMERUN_DEVICE_TYPE_TUNER, HDHOMERUN_DEVICE_ID_WILDCARD, result_list, 64);
      
        if (count <= 0) {
            hdhomerun_discover_destroy(ds);
            return tuners; 
        }
      
        for (int i = 0; i < count; i++) {
            uint32_t ip = result_list[i].ip_addr;
            char ip_str[32];            
            sprintf(ip_str, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
            tuners.push_back(std::string(ip_str));
        }
        hdhomerun_discover_destroy(ds);
        return tuners;
    }



	bool IsRemoteFileNewer(const std::string& url, const std::string& localPath) {
	    if (cfg.debugEnable) std::printf("[DVR DEBUG] Checking local time-window cache status...\n");
	
	    struct stat attrib;
	    if (stat(localPath.c_str(), &attrib) != 0) {
	        if (cfg.debugEnable) std::printf("[DVR DEBUG] Local XML file missing. Forcing download.\n");
	        return true; 
	    }
	
	    std::string cacheControlPath = localPath + ".cache";
	    std::ifstream cacheIn(cacheControlPath);
	    uint32 savedSyncTime = 0;
	    
	    if (cacheIn.is_open()) {
	        cacheIn >> savedSyncTime;
	        cacheIn.close();
	    }
	
	    // Set a 3-day expiration window for the master payload database
	    uint32 cacheExpirationWindow = 259200; 
	    uint32 currentTime = real_time_clock();
	
	    if (savedSyncTime > 0 && (currentTime - savedSyncTime) < cacheExpirationWindow) {
	        if (cfg.debugEnable) {
	            uint32 remainingTime = cacheExpirationWindow - (currentTime - savedSyncTime);
	            std::printf("[DVR DEBUG] SUCCESS: Master cache is %u secs old (Expires in %u mins). Skipping network.\n", 
	                        (currentTime - savedSyncTime), remainingTime / 60);
	        }
	        return false; 
	    }
	
	    if (cfg.debugEnable) std::printf("[DVR DEBUG] Cache window expired or invalid for master payload. Ready to sync with server.\n");
	
	    // =========================================================================
	    //  AUTOMATED CACHE PURGE ENGINE (REMOVES EXPIRED DAILY CHUNK FILES)
	    // =========================================================================
	    std::printf("[DVR PURGE] Running storage sweep routine on expired XML chunks...\n");
	    
	    // Get today's date formatted as an integer integer key (e.g., 20260624)
	    std::time_t rawToday = std::time(nullptr);
	    std::tm* localToday = std::localtime(&rawToday);
	    char todayBuf[16];
	    std::strftime(todayBuf, sizeof(todayBuf), "%Y%m%d", localToday);
	    long todayIntKey = std::atol(todayBuf);
	
	    std::string configDirectoryPath = "/boot/home/config/settings/HaikuDVR";
	    DIR* dir = opendir(configDirectoryPath.c_str());
	    if (dir != nullptr) {
	        struct dirent* entry;
	        uint32 deletedFilesCount = 0;
	
	        while ((entry = readdir(dir)) != nullptr) {
	            std::string filename(entry->d_name);
	            
	            if (filename.rfind("guide_", 0) == 0 && filename.length() == 18) { 
	                std::string datePart = filename.substr(6, 8);
	                
	                if (datePart.find_first_not_of("0123456789") == std::string::npos) {
	                    long fileDateIntKey = std::atol(datePart.c_str());
	                    
	                    if (fileDateIntKey < todayIntKey) {
	                        std::string fullPathToDelete = configDirectoryPath + "/" + filename;
	                        if (unlink(fullPathToDelete.c_str()) == 0) {
	                            deletedFilesCount++;
	                        }
	                    }
	                }
	            }
	        }
	        closedir(dir);
	        std::printf("[DVR PURGE] Storage sweep complete. Purged %u obsolete history file chunks.\n", deletedFilesCount);
	    }
	    // =========================================================================
	
	    return true;
	}



	bool IngestMasterXmlToSqlite(const std::string& masterXmlPath, const std::string& dbPath) {
	    sqlite3* db = nullptr;
	    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
	        std::printf("[DVR DB ERROR] Failed to open database.\n");
	        return false;
	    }
	
	    // 1. Establish the database structures and performance options
	    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
	    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
	    sqlite3_exec(db, "PRAGMA cache_size = -8000;", nullptr, nullptr, nullptr); 
	    
	    const char* schema = 
	        "CREATE TABLE IF NOT EXISTS channels (xml_id TEXT PRIMARY KEY, lcn TEXT, icon_url TEXT);"
	        "CREATE TABLE IF NOT EXISTS programs (channel_id TEXT, title TEXT, desc TEXT, start_epoch INTEGER, end_epoch INTEGER);"
	        "CREATE INDEX IF NOT EXISTS idx_channels_lcn_lookup ON channels (lcn, xml_id, icon_url);"
	        "CREATE INDEX IF NOT EXISTS idx_programs_timeline_covering ON programs (start_epoch, end_epoch, channel_id, title, [desc]);";
	    
	    if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
	        sqlite3_close(db);
	        return false;
	    }
	
	    // Clear old guide schedules to prepare for fresh data injection
	    sqlite3_exec(db, "DELETE FROM programs;", nullptr, nullptr, nullptr);
	
	    std::ifstream masterStream(masterXmlPath.c_str());
	    if (!masterStream.is_open()) {
	        sqlite3_close(db);
	        return false;
	    }
	
	    auto parseXmlTimeToEpoch = [](const std::string& rawXmlTime) -> std::time_t {
	        if (rawXmlTime.length() < 14) return 0;
	        int y = 0, mo = 0, d = 0, h = 0, m = 0, s = 0;
	        std::sscanf(rawXmlTime.substr(0, 4).c_str(), "%4d", &y);
	        std::sscanf(rawXmlTime.substr(4, 2).c_str(), "%2d", &mo);
	        std::sscanf(rawXmlTime.substr(6, 2).c_str(), "%2d", &d);
	        std::sscanf(rawXmlTime.substr(8, 2).c_str(), "%2d", &h);
	        std::sscanf(rawXmlTime.substr(10, 2).c_str(), "%2d", &m);
	        std::sscanf(rawXmlTime.substr(12, 2).c_str(), "%2d", &s);
	
	        std::tm tmTime = {0};
	        tmTime.tm_year  = y - 1900;
	        tmTime.tm_mon   = mo - 1;
	        tmTime.tm_mday  = d;
	        tmTime.tm_hour  = h;
	        tmTime.tm_min   = m;
	        tmTime.tm_sec   = s;
	        tmTime.tm_isdst = -1;
	
	        std::time_t utcEpoch = timegm(&tmTime);
	        if (utcEpoch == (std::time_t)-1) return 0;
	        
	        long providerOffsetSeconds = 0;
	        size_t spacePos = rawXmlTime.find(' ');
	        if (spacePos != std::string::npos && spacePos + 5 <= rawXmlTime.length()) {
	            int sign = (rawXmlTime[spacePos + 1] == '-') ? -1 : 1;
	            int oh = 0, om = 0;
	            std::sscanf(rawXmlTime.substr(spacePos + 2, 2).c_str(), "%2d", &oh);
	            std::sscanf(rawXmlTime.substr(spacePos + 4, 2).c_str(), "%2d", &om);
	            providerOffsetSeconds = sign * ((oh * 3600) + (om * 60));
	        }
	        return utcEpoch - providerOffsetSeconds;
	    };
	
	    // Prepare separate compiled statements for independent, high-performance insertion/updates
	    sqlite3_stmt* chanInitStmt = nullptr;
	    sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO channels (xml_id) VALUES (?);", -1, &chanInitStmt, nullptr);
	
	    sqlite3_stmt* chanLcnStmt = nullptr;
	    sqlite3_prepare_v2(db, "UPDATE channels SET lcn = ? WHERE xml_id = ?;", -1, &chanLcnStmt, nullptr);
	
	    sqlite3_stmt* chanIconStmt = nullptr;
	    sqlite3_prepare_v2(db, "UPDATE channels SET icon_url = ? WHERE xml_id = ?;", -1, &chanIconStmt, nullptr);
	
	    sqlite3_stmt* progStmt = nullptr;
	    sqlite3_prepare_v2(db, "INSERT INTO programs VALUES (?, ?, ?, ?, ?);", -1, &progStmt, nullptr);
	
	    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
	
	    std::string line;
	    std::string currentChannelId = "";
	    std::string progChanId = "", progStartRaw = "", progEndRaw = "", titleText = "", descText = "";
	    bool insideProgramme = false;
	    std::time_t current_time = std::time(nullptr);
	    while (std::getline(masterStream, line)) {
	        // Parse channels
	        size_t chanPos = line.find("<channel id=\"");
	        if (chanPos != std::string::npos) {
	            size_t startIdx = chanPos + 13;
	            size_t endIdx = line.find("\"", startIdx);
	            if (endIdx != std::string::npos) {
	                currentChannelId = line.substr(startIdx, endIdx - startIdx);
	                
	                // Initialize the channel entry safely without touching old records
	                sqlite3_bind_text(chanInitStmt, 1, currentChannelId.c_str(), -1, SQLITE_TRANSIENT);
	                sqlite3_step(chanInitStmt);
	                sqlite3_reset(chanInitStmt);
	            }
	            continue;
	        }
	
	        size_t lcnPos = line.find("<lcn>");
	        if (lcnPos != std::string::npos && !currentChannelId.empty()) {
	            size_t endIdx = line.find("</lcn>");
	            if (endIdx != std::string::npos) {
	                std::string lcnVal = line.substr(lcnPos + 5, endIdx - (lcnPos + 5));
	                
	                // Update LCN cleanly
	                sqlite3_bind_text(chanLcnStmt, 1, lcnVal.c_str(), -1, SQLITE_TRANSIENT);
	                sqlite3_bind_text(chanLcnStmt, 2, currentChannelId.c_str(), -1, SQLITE_TRANSIENT);
	                sqlite3_step(chanLcnStmt);
	                sqlite3_reset(chanLcnStmt);
	            }
	            continue;
	        }
	
	        size_t iconPos = line.find("<icon src=\"");
	        if (iconPos != std::string::npos && !currentChannelId.empty()) {
	            size_t endIdx = line.find("\"", iconPos + 11);
	            if (endIdx != std::string::npos) {
	                std::string iconUrl = line.substr(iconPos + 11, endIdx - (iconPos + 11));
	                
	                // Update Icon URL cleanly without disturbing the LCN value
	                sqlite3_bind_text(chanIconStmt, 1, iconUrl.c_str(), -1, SQLITE_TRANSIENT);
	                sqlite3_bind_text(chanIconStmt, 2, currentChannelId.c_str(), -1, SQLITE_TRANSIENT);
	                sqlite3_step(chanIconStmt);
	                sqlite3_reset(chanIconStmt);
	            }
	            currentChannelId = "";
	            continue;
	        }
	
	
	
	     // Parse program blocks securely
	        size_t progPos = line.find("<programme start=\"");
	        if (progPos != std::string::npos) {
	            insideProgramme = true;
	            titleText = ""; descText = "";
	            
	            size_t sEnd = line.find("\"", progPos + 18);
	            if (sEnd != std::string::npos) progStartRaw = line.substr(progPos + 18, sEnd - (progPos + 18));
	
	            size_t stopPos = line.find("stop=\"");
	            if (stopPos != std::string::npos) {
	                size_t eEnd = line.find("\"", stopPos + 6);
	                if (eEnd != std::string::npos) progEndRaw = line.substr(stopPos + 6, eEnd - (stopPos + 6));
	            }
	
	            size_t chanAttrPos = line.find("channel=\"");
	            if (chanAttrPos != std::string::npos) {
	                size_t cEnd = line.find("\"", chanAttrPos + 9);
	                if (cEnd != std::string::npos) progChanId = line.substr(chanAttrPos + 9, cEnd - (chanAttrPos + 9));
	            }
	            continue;
	        }
	
	        if (insideProgramme) {
	            size_t titlePos = line.find("<title");
	            if (titlePos != std::string::npos) {
	                size_t valStart = line.find(">", titlePos) + 1;
	                size_t valEnd = line.find("</title>", valStart);
	                if (valEnd != std::string::npos) titleText = line.substr(valStart, valEnd - valStart);
	            }
	            size_t descPos = line.find("<desc");
	            if (descPos != std::string::npos) {
	                size_t valStart = line.find(">", descPos) + 1;
	                size_t valEnd = line.find("</desc>", valStart);
	                if (valEnd != std::string::npos) descText = line.substr(valStart, valEnd - valStart);
	            }
	
	            if (line.find("</programme>") != std::string::npos) {
	                insideProgramme = false;
	                std::time_t startEp = parseXmlTimeToEpoch(progStartRaw);
	                std::time_t endEp   = parseXmlTimeToEpoch(progEndRaw);
	
	                if (endEp < startEp) endEp += 86400; // Account for overnight date boundary wraps
	
	                // THE FILTER PASS: If the show's end time is in the past, skip SQLite execution
	                if (endEp < current_time) {
	                    continue; 
	                }
	
	                if (!progChanId.empty() && !titleText.empty()) {
	                    sqlite3_bind_text(progStmt, 1, progChanId.c_str(), -1, SQLITE_TRANSIENT);
	                    sqlite3_bind_text(progStmt, 2, titleText.c_str(), -1, SQLITE_TRANSIENT);
	                    sqlite3_bind_text(progStmt, 3, descText.c_str(), -1, SQLITE_TRANSIENT);
	                    sqlite3_bind_int64(progStmt, 4, static_cast<sqlite3_int64>(startEp));
	                    sqlite3_bind_int64(progStmt, 5, static_cast<sqlite3_int64>(endEp));
	                    sqlite3_step(progStmt);
	                    sqlite3_reset(progStmt);
	                }
	            }
	        }
	
	    }
	
	    // Close transaction and finalize all prepared statements
	    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
	    
	    // Finalize all three distinct channel statement resources
	    sqlite3_finalize(chanInitStmt);
	    sqlite3_finalize(chanLcnStmt);
	    sqlite3_finalize(chanIconStmt);
	    sqlite3_finalize(progStmt);
	
	    // Run internal structural analysis optimization before releasing file lock
	    sqlite3_exec(db, "PRAGMA optimize;", nullptr, nullptr, nullptr);
	
	    sqlite3_close(db);
	    masterStream.close();
	    return true;
	}


	// =========================================================================
	//  DATABASE GRID ROUTINE: QUERIES SQLITE FOR THE 4-BUCKET TIMELINE WINDOW
	// =========================================================================
	void PopulateGridFromDatabase(const std::string& dbPath, int targetYear, int targetMonth, int targetDay, int targetHour, int targetMin) {
	    sqlite3* db = nullptr;
	    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
	        if (cfg.debugEnable)  std::printf("[DVR DB DEBUG] FATAL: Cannot open database file at %s\n", dbPath.c_str());
	        return;
	    }
	
	    // =========================================================================
	    // DIAGNOSTIC CHECK: PRINT ACTIVE TABLES AND PROBE RAW STORAGE DATA
	    // =========================================================================
	   if (cfg.debugEnable)  std::printf("[DVR DB DEBUG] --- CURRENT DATABASE TABLE CATALOG ---\n");
	    sqlite3_stmt* masterQuery = nullptr;
	    const char* catalogSql = "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;";
	    
	    if (sqlite3_prepare_v2(db, catalogSql, -1, &masterQuery, nullptr) == SQLITE_OK) {
	        while (sqlite3_step(masterQuery) == SQLITE_ROW) {
	            if (cfg.debugEnable)  std::printf("  -> Found Table: %s\n", (const char*)sqlite3_column_text(masterQuery, 0));
	        }
	        sqlite3_finalize(masterQuery);
	    }
	    if (cfg.debugEnable) std::printf("[DVR DB DEBUG] ----------------------------------------\n");
	
	    // NEW DATA PROBE: Pull the first 3 rows from the programs table to see if description data exists
	   if (cfg.debugEnable)  std::printf("[DVR DATA PROBE] Reading raw rows from 'programs' table...\n");
	    sqlite3_stmt* probeStmt = nullptr;
	    const char* probeSql = "SELECT channel_id, title, [desc] FROM programs LIMIT 3;";
	    
	    if (sqlite3_prepare_v2(db, probeSql, -1, &probeStmt, nullptr) == SQLITE_OK) {
	        int rowIdx = 0;
	        while (sqlite3_step(probeStmt) == SQLITE_ROW) {
	            rowIdx++;
	            const char* chId  = (const char*)sqlite3_column_text(probeStmt, 0);
	            const char* title = (const char*)sqlite3_column_text(probeStmt, 1);
	            const char* rawDesc = (const char*)sqlite3_column_text(probeStmt, 2);
	            std::string desc  = rawDesc ? rawDesc : "[NULL POINTER]";
	          if (cfg.debugEnable) {  
	            std::printf("  -> Row %d | Ch: %s | Title: %s\n", rowIdx, chId ? chId : "", title ? title : "");
	            std::printf("     Description: '%s'\n", desc.empty() ? "[EMPTY STRING]" : desc.c_str());
	          }
	        }
	        if (rowIdx == 0) {
	           if (cfg.debugEnable)  std::printf("  -> [WARNING]: The 'programs' table is completely empty!\n");
	        }
	        sqlite3_finalize(probeStmt);
	    } else {
	       if (cfg.debugEnable)  std::printf("  -> [ERROR]: Query preparation failed for data probe! Table might be structurally corrupt.\n");
	    }
	   if (cfg.debugEnable) std::printf("[DVR DATA PROBE] ----------------------------------------\n");
	
	    cloudGuideMap.clear();
	    xmlIdToChannelNumMap.clear();
	    cloudIconMap.clear();
	
	    // Rebuild basic station and tuner asset mappings from SQLite
	    sqlite3_stmt* chanQuery = nullptr;
	    const char* chanSql = "SELECT xml_id, lcn, icon_url FROM channels WHERE lcn IS NOT NULL;";
	    
	    if (sqlite3_prepare_v2(db, chanSql, -1, &chanQuery, nullptr) == SQLITE_OK) {
	        while (sqlite3_step(chanQuery) == SQLITE_ROW) {
	            std::string xmlId  = (const char*)sqlite3_column_text(chanQuery, 0);
	            std::string chNum  = (const char*)sqlite3_column_text(chanQuery, 1);
	            const char* iconUrl = (const char*)sqlite3_column_text(chanQuery, 2);
	
	            xmlIdToChannelNumMap[xmlId] = chNum;
	            if (iconUrl) {
	                cloudIconMap[chNum] = iconUrl;
	            }
	
	            if (cloudGuideMap.find(chNum) == cloudGuideMap.end()) {
	                ChannelGuideItem item;
	                item.guideNumber = chNum;
	                item.nowPlaying  = "To Be Announced";
	                item.nowPlayingDurationMinutes = 30;
	                cloudGuideMap[chNum] = item;
	            }
	        }
	        sqlite3_finalize(chanQuery);
	    }
	
	    // Loop through your 4 layout grid buckets sequentially
	    for (int32 bucket = 0; bucket < 4; bucket++) {
	        
	        std::tm bucketTimeBox = {0};
	        bucketTimeBox.tm_year = targetYear - 1900;
	        bucketTimeBox.tm_mon  = targetMonth - 1;
	        bucketTimeBox.tm_mday = targetDay;
	        bucketTimeBox.tm_hour = targetHour;
	        bucketTimeBox.tm_min  = targetMin + (bucket * 30); 
	        bucketTimeBox.tm_sec  = 0;
	        bucketTimeBox.tm_isdst = -1;
	
	        std::time_t bucketTargetEpoch = std::mktime(&bucketTimeBox);
	        std::time_t cellEndEpoch      = bucketTargetEpoch + 1800; 
	
	        // ESCAPED QUERY: Ensure square brackets isolate the reserved descriptor keyword
	        const char* progSql = 
	            "SELECT channel_id, title, [desc], start_epoch, end_epoch FROM programs "
	            "WHERE start_epoch < ? AND end_epoch > ?;";
	
	        sqlite3_stmt* progQuery = nullptr;
	        int prepareResult = sqlite3_prepare_v2(db, progSql, -1, &progQuery, nullptr);
	        
	        if (prepareResult == SQLITE_OK) {
	            sqlite3_bind_int64(progQuery, 1, cellEndEpoch);
	            sqlite3_bind_int64(progQuery, 2, bucketTargetEpoch);
	
	                     while (sqlite3_step(progQuery) == SQLITE_ROW) {
	                std::string xmlId        = (const char*)sqlite3_column_text(progQuery, 0);
	                std::string titleText    = (const char*)sqlite3_column_text(progQuery, 1);
	                const char* rawDesc      = (const char*)sqlite3_column_text(progQuery, 2);
	                std::string descText     = rawDesc ? rawDesc : "";
	                std::time_t progStart    = static_cast<std::time_t>(sqlite3_column_int64(progQuery, 3));
	                std::time_t progEnd      = static_cast<std::time_t>(sqlite3_column_int64(progQuery, 4));
	
	                std::string associatedChNum = xmlIdToChannelNumMap[xmlId];
	                if (associatedChNum.empty()) {
	                    if (cfg.debugEnable) {
	                        std::printf("[DVR GRID ERROR] Skipping row: XML ID '%s' has no mapped logical channel number.\n", xmlId.c_str());
	                    }
	                    continue;
	                }
	
	                auto& activeItem = cloudGuideMap[associatedChNum];
	
	                // =========================================================================
	                // DEBUG TRACE A: DISPLAY ROW PAYLOAD FRESH OUT OF SQLITE
	                // =========================================================================
	                if (cfg.debugEnable && (titleText.find("Fallon") != std::string::npos || titleText.find("Kimmel") != std::string::npos)) {
	                    std::printf("[DVR GRID TRACE] SQL row read -> Ch: %s | Title: %s | Desc Length: %lu | Characters: '%s'\n", 
	                                associatedChNum.c_str(), titleText.c_str(), (unsigned long)descText.length(), 
	                                (descText.length() > 30 ? descText.substr(0, 30).c_str() : descText.c_str()));
	                }
	                // =========================================================================
	
	                std::tm displayTime = {}, endTm = {};
	                localtime_r(&bucketTargetEpoch, &displayTime);
	                localtime_r(&progEnd, &endTm);
	                
	                char timeBuf[32], endBuf[32];
	                std::strftime(timeBuf, sizeof(timeBuf), "%I:%M %p", &displayTime);
	                std::strftime(endBuf, sizeof(endBuf), "%I:%M %p", &endTm);
	
	                BString formattedTimeStr(timeBuf), formattedEndTimeStr(endBuf);
	                if (formattedTimeStr.StartsWith("0")) formattedTimeStr.Remove(0, 1);
	                if (formattedEndTimeStr.StartsWith("0")) formattedEndTimeStr.Remove(0, 1);
	
	                if (bucket == 0) {
	                    activeItem.nowPlaying = titleText;
	                    activeItem.nowPlayingDurationMinutes = (int32)((progEnd - progStart) / 60);
	                    activeItem.nowPlayingEndTimeStr = formattedEndTimeStr.String();
	                    
	                    // Convert std::string to const char* using .c_str()
	                    activeItem.nowPlayingDescription = descText.c_str(); 
	                } else {
	                    if (activeItem.futureLineup.size() < static_cast<size_t>(bucket)) {
	                        activeItem.futureLineup.resize(bucket);
	                    }
	
	                    if (!activeItem.futureLineup[bucket - 1].title.empty() && 
	                        activeItem.futureLineup[bucket - 1].title != "To Be Announced") {
	                        
	                        if (activeItem.futureLineup[bucket - 1].description.IsEmpty()) {
	                            // FIX 2: Convert std::string to const char* using .c_str()
	                            activeItem.futureLineup[bucket - 1].description = descText.c_str();
	                        }
	                        continue; 
	                    }
	
	                    UpcomingShowItem futureShow;
	                    futureShow.title = titleText;
	                    futureShow.durationMinutes = (int32)((progEnd - progStart) / 60);
	                    futureShow.startTimeStr = formattedTimeStr.String();
	                    futureShow.endTimeStr = formattedEndTimeStr.String();
	                    
	                    // Convert std::string to const char* using .c_str()
	                    futureShow.description = descText.c_str(); 
	
	                    activeItem.futureLineup[bucket - 1] = futureShow;
	               
	
	                    
	                    // =========================================================================
	                    // 🔍 DEBUG TRACE D: VERIFY FUTURE LINEUP MATRIX ASSIGNMENT
	                    // =========================================================================
	                    if (cfg.debugEnable && (titleText.find("Fallon") != std::string::npos || titleText.find("Kimmel") != std::string::npos)) {
	                        std::printf("[DVR GRID TRACE] Assigned Bucket %d sub-show info payload description target.\n", bucket);
	                    }
	                    // =========================================================================
	                }
	            }
	
	            sqlite3_finalize(progQuery);
	        } else {
	            if (cfg.debugEnable) std::printf("[DVR DB DEBUG] Bucket %d SQL Statement Prepare Failed! Error code: %d\n", bucket, prepareResult);
	        }
	
	        // Pad unassigned column buckets with generic placeholders to prevent runtime indexing errors
	        for (auto& [chNum, activeItem] : cloudGuideMap) {
	            if (bucket > 0) {
	                if (activeItem.futureLineup.size() < static_cast<size_t>(bucket)) {
	                    activeItem.futureLineup.resize(bucket);
	                }
	                if (activeItem.futureLineup[bucket - 1].title.empty()) {
	                    activeItem.futureLineup[bucket - 1].title = "To Be Announced";
	                    activeItem.futureLineup[bucket - 1].durationMinutes = 30;
	                }
	            }
	        }
	    }
	
	    sqlite3_close(db);
	}

                



	void FetchAndPopulateChannelList(const char* targetDateStr = nullptr) {
	
	    fChannelListView->MakeEmpty();
	    fLoadedChannels.clear();
	
	    std::string targetDateOnly = (targetDateStr != nullptr && std::strlen(targetDateStr) > 0) ? targetDateStr : "";
	    if (targetDateOnly.empty()) {
	        std::time_t rawToday = std::time(nullptr);
	        std::tm* localToday = std::localtime(&rawToday);
	        char todayBuf[32];
	        std::strftime(todayBuf, sizeof(todayBuf), "%Y-%m-%d", localToday);
	        targetDateOnly = todayBuf;
	    }
	
	    std::string cleanTargetDate = targetDateOnly;
	    cleanTargetDate.erase(std::remove(cleanTargetDate.begin(), cleanTargetDate.end(), '-'), cleanTargetDate.end());
	
	    int targetYear = 2026, targetMonth = 6, targetDay = 21;
	    std::sscanf(targetDateOnly.c_str(), "%d-%d-%d", &targetYear, &targetMonth, &targetDay);
	
	    int targetHour = 12, targetMin = 0;
	    if (fTimeInput != nullptr && fTimeInput->Text() != nullptr) {
	        int parsedHour = 12, parsedMin = 0;
	        BString rawTimeText(fTimeInput->Text());
	        if (std::sscanf(rawTimeText.String(), "%d:%d", &parsedHour, &parsedMin) >= 1) {
	            targetHour = parsedHour;
	            targetMin = parsedMin;
	            
	            // =========================================================================
	            //  FIXED: 24-HOUR MILITARY TIME ENFORCEMENT
	            // =========================================================================
	            bool hasPM = (rawTimeText.IFindFirst("PM") != B_ERROR);
	            bool hasAM = (rawTimeText.IFindFirst("AM") != B_ERROR);
	
	            if (hasPM && targetHour < 12) {
	                targetHour += 12;
	            } else if (hasAM && targetHour == 12) {
	                targetHour = 0;
	            } else if (!hasAM && !hasPM) {
	                // If no explicit AM or PM flag exists in the raw input string:
	                if (targetHour >= 1 && targetHour <= 4) {
	                    // Assume 1 through 4 implies afternoon hours (1 PM - 4 PM)
	                    targetHour += 12;
	                }
	                // Prime morning slots (5 through 11) are left alone as AM values
	            }
	            // =========================================================================
	        }
	    }
	
	    if (targetMin < 30) {
	        targetMin = 0;
	    } else {
	        targetMin = 30;
	    }
	
	    // =========================================================================
	    //  CONFIGURATION CORRECTION: SEPARATE MASTER CACHE FROM THE TARGET CHUNK
	    // =========================================================================
	
	    std::string xmlCachePath = "/boot/home/config/settings/HaikuDVR/guide.xml"; 
	    std::string masterXmlPath = "/boot/home/config/settings/HaikuDVR/guide_master.xml"; 
	    std::string targetChunkPath = "/boot/home/config/settings/HaikuDVR/guide_" + cleanTargetDate + ".xml"; 
	
	    // =========================================================================
	    // THREAD-SAFE HEADER SYNC PASS (PREVENTS DEADLOCKS SYSTEM-WIDE)
	    // =========================================================================
	    BView* headerView = FindView("timelineHeader");
	    if (headerView != nullptr) {
	        BMessage syncHeaderMsg('UCLT');
	        
	        if (fTimeInput != nullptr && fTimeInput->Text() != nullptr) {
	            syncHeaderMsg.AddString("time", fTimeInput->Text());
	        }
	        
	        syncHeaderMsg.AddString("date", targetDateOnly.c_str());        
	        BMessenger(headerView).SendMessage(&syncHeaderMsg);
	    }
	
	    // =========================================================================
	
	    std::vector<std::string> tuners = DiscoverAllTuners();
	    if (tuners.empty()) {
	        fStatusLabel->SetText("Status: Guide Error - No active tuners discovered.");
	        return;
	    }
	    std::string targetIp = fSelectedIp.empty() ? tuners[0] : fSelectedIp;
	
	    // Check master file status on disk
	    struct stat masterStat;
	    bool masterMissing = (stat(masterXmlPath.c_str(), &masterStat) != 0);
	
	    // Check if the specific daily target chunk file is missing
	    std::ifstream checkChunk(targetChunkPath.c_str());
	    bool dailyChunkMissing = !checkChunk.good();
	    if (checkChunk.is_open()) {
	        checkChunk.close();
	    }
	
	    std::string xmltvUrlDummy = "https://hdhomerun.com";
	    bool masterCacheExpired = IsRemoteFileNewer(xmltvUrlDummy, masterXmlPath);
	
	    bool needNetworkFetch = fCachedGuidePayload.empty() || fLastNetworkSyncTime == 0 || dailyChunkMissing || masterMissing || masterCacheExpired;
	    
	    // =========================================================================
	    //  PRESERVATION ON LOCAL CACHE MATCH
	    // =========================================================================
	    if (!needNetworkFetch) {
	        fCachedGuidePayload = "LOADED";
	        if (fLastNetworkSyncTime == 0) {
	            fLastNetworkSyncTime = masterStat.st_mtime;
	        }
	    }
	    // =========================================================================
	
	    if (needNetworkFetch) {
	        std::string discoveryUrl = "http://" + targetIp + "/discover.json";
	        std::string discoverPayload;
	        CURL* curl = curl_easy_init();
	        if (curl) {
	            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 HaikuDVR/1.0");
	            curl_easy_setopt(curl, CURLOPT_URL, discoveryUrl.c_str());
	            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
	            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &discoverPayload);
	            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
	            if (curl_easy_perform(curl) != CURLE_OK) {
	                fStatusLabel->SetText("Status: Local tuner discovery failed.");
	            }
	            curl_easy_cleanup(curl);
	        }
	
	        std::string deviceAuthToken = "";
	        try {
	            auto jDisc = json::parse(discoverPayload);
	            if (jDisc.is_object() && jDisc.contains("DeviceAuth")) {
	                deviceAuthToken = jDisc["DeviceAuth"].get<std::string>();
	            }
	        } catch (...) {}
	
	        if (!deviceAuthToken.empty()) {          
	            std::string xmltvUrl = "https://api.hdhomerun.com/api/xmltv?DeviceAuth=" + deviceAuthToken;
	       
	            if (!masterCacheExpired) {
	                fLastNetworkSyncTime = real_time_clock();
	                fCachedGuidePayload = "LOADED";
	                needNetworkFetch = false; 
	            } else {
	                needNetworkFetch = true;
	            }
	
	            if (needNetworkFetch) {
	                FILE* xmlFilePtr = std::fopen(masterXmlPath.c_str(), "wb");
	                curl = curl_easy_init();
	                if (curl && xmlFilePtr) {
	                    curl_easy_setopt(curl, CURLOPT_URL, xmltvUrl.c_str());
	                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 HaikuDVR/1.0");
	                    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
	                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, xmlFilePtr);
	                    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
	                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); 
	                    
	                    CURLcode res = curl_easy_perform(curl);
	                    
	                    if (res == CURLE_OK) {
	                        uint32 syncTimestamp = real_time_clock();
	                        fLastNetworkSyncTime = syncTimestamp;
	                        fCachedGuidePayload = "LOADED";
	                        
	                        std::string cacheControlPath = masterXmlPath + ".cache"; 
	                        std::ofstream cacheOut(cacheControlPath);
	                        if (cacheOut.is_open()) {
	                            cacheOut << syncTimestamp << "\n";
	                            cacheOut.close();
	                            if (cfg.debugEnable) std::printf("[DVR DEBUG] Saved fresh sync timestamp token: %u\n", syncTimestamp);
	                        }
	                    }
	
	                    curl_easy_cleanup(curl);
	                    std::fclose(xmlFilePtr);
	    
	                    if (res != CURLE_OK) {
	                        fStatusLabel->SetText("Status: Cloud XMLTV download failed.");
	                        return;
	                    }
	                } else if (xmlFilePtr) {
	                    std::fclose(xmlFilePtr);
	                }
	            }
	        }
	    }
	
	    // =========================================================================
	    //  🎯 SQLITE DB SYNC: AUTO-INGEST & POPULATE BACKEND DATA STRUCTURES
	    // =========================================================================
	    std::string dbPath = "/boot/home/config/settings/HaikuDVR/guide.db";
	
	    struct stat dbStat;
	    bool dbIsEmpty = (stat(dbPath.c_str(), &dbStat) != 0 || dbStat.st_size == 0);
	
	    // If the database doesn't exist, or if we just pulled down a fresh masterXml chunk, sync it
	    if (dbIsEmpty || needNetworkFetch) {
	        if (cfg.debugEnable) std::printf("[DVR DB] SQLite database empty or master XML was updated. Synchronizing storage records...\n");
	        IngestMasterXmlToSqlite(masterXmlPath, dbPath);
	    }
	
	    // Fire the high-performance database grid lookup utility pass
	    PopulateGridFromDatabase(dbPath, targetYear, targetMonth, targetDay, targetHour, targetMin);
	
	
	
	    // -------------------------------------------------------------------------
	    // PASS 3: DIRECT NETWORK LINEUP STREAMING PIPELINE (TESTING PASS)
	    // -------------------------------------------------------------------------
	    fLoadedChannels.clear(); // Clear local display lists
	    
	    std::string lineupUrl = "http://" + targetIp + "/lineup.json";
	    std::string lineupPayload = ""; // Clear string tracking allocations in RAM
	    
	    if (cfg.debugEnable) std::printf("[DVR TEST] Directly streaming tuner lineup from: %s\n", lineupUrl.c_str());
	
	    CURL* curlLineup = curl_easy_init();
	    if (curlLineup) {
	        curl_easy_setopt(curlLineup, CURLOPT_URL, lineupUrl.c_str());
	        curl_easy_setopt(curlLineup, CURLOPT_USERAGENT, "Mozilla/5.0 HaikuDVRFrontend/1.0");
	        
	        // Use your working StringWriteCallback to append bytes straight into RAM
	        curl_easy_setopt(curlLineup, CURLOPT_WRITEFUNCTION, NetworkStringCallback);
	        curl_easy_setopt(curlLineup, CURLOPT_WRITEDATA, &lineupPayload);
	        
	        curl_easy_setopt(curlLineup, CURLOPT_TIMEOUT, 4L); 
	        CURLcode res = curl_easy_perform(curlLineup);
	        curl_easy_cleanup(curlLineup);
	
	        if (res != CURLE_OK) {
	            std::printf("[DVR TEST ERROR] Network stream failed! Curl Code: %d\n", res);
	        }
	    }
	
	    try {
	        // Parse the live payload directly out of system RAM
	        auto jLineup = json::parse(lineupPayload);
	        
	        if (jLineup.is_array()) {
	            if (cfg.debugEnable) std::printf("[DVR TEST] Successfully parsed %lu live network channel rows.\n", (unsigned long)jLineup.size());
	            
	            for (const auto& channelEntry : jLineup) {
	                std::string chNum = channelEntry.value("GuideNumber", "0.0");
	                
	                int isRealHD = channelEntry.value("HD", 0);
	                if (fCurrentFilter == FILTER_HD && isRealHD == 0) continue;
	                if (fCurrentFilter == FILTER_SD && isRealHD == 1) continue;
	
	                ChannelGuideItem finalItem;
	                
	                // Read from your current SQLite-populated map structures seamlessly
	                if (cloudGuideMap.find(chNum) != cloudGuideMap.end()) {
	                    finalItem = cloudGuideMap[chNum];
	                    if (finalItem.guideName.empty()) {
	                        finalItem.guideName = channelEntry.value("GuideName", "Unknown");
	                    }
	                } else {
	                    // Safe database-mismatch fallback: Keep the network channel visible
	                    finalItem.guideNumber = chNum;
	                    finalItem.guideName   = channelEntry.value("GuideName", "Unknown");
	                    finalItem.nowPlaying  = "To Be Announced";
	                    finalItem.nowPlayingDurationMinutes = 30;
	                }
	
	                int32 activeListRowIndex = (int32)fLoadedChannels.size();
	                fLoadedChannels.push_back(finalItem);
	
	                // Isolate image assets from the raw JSON payload
	                std::string downloadUrl = "";
	                if (channelEntry.contains("ImageURL")) {
	                    downloadUrl = channelEntry["ImageURL"].get<std::string>();
	                } else if (cloudIconMap.find(chNum) != cloudIconMap.end()) {
	                    downloadUrl = cloudIconMap[chNum];
	                }
	
	                if (!downloadUrl.empty()) {
	                    std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + finalItem.guideName + ".png";
	                    
	                    std::ifstream checkFile(iconPath.c_str());
	                    bool fileExistsOnDisk = checkFile.good();
	                    checkFile.close();
	
	                    if (!fileExistsOnDisk) {
	                        gIconQueueLocker.Lock();
	                        DownloadQueueItem job = { iconPath, downloadUrl, activeListRowIndex };
	                        gIconDownloadQueue.push_back(job);
	                        gIconQueueLocker.Unlock();
	                    }
	                }
	            }
	        }
	    } catch (...) {
	        fStatusLabel->SetText("Status: Local tuner lineup filter network processing failed.");
	        std::printf("[DVR TEST ERROR] JSON exception caught parsing live tuner payload string.\n");
	    }
	
	
	    // -------------------------------------------------------------------------
	    // PASS 4: POPULATE RENDERING LIST LAYER AND INITIALIZE ASSETS
	    // -------------------------------------------------------------------------
	    for (size_t i = 0; i < fIconCache.size(); i++) {
	        delete fIconCache[i];
	    }
	    fIconCache.clear();
	
	    for (size_t i = 0; i < fLoadedChannels.size(); i++) {
	        const auto& item = fLoadedChannels[i];
	
	        std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + item.guideName + ".png";
	        
	        std::ifstream checkFile(iconPath.c_str());
	        bool fileExistsOnDisk = checkFile.good();
	        checkFile.close();
	
	        if (!fileExistsOnDisk && cloudIconMap.find(item.guideNumber) != cloudIconMap.end()) {
	            std::string downloadUrl = cloudIconMap[item.guideNumber];
	            gIconQueueLocker.Lock();
	            DownloadQueueItem job = { iconPath, downloadUrl, -1 };
	            gIconDownloadQueue.push_back(job);
	            gIconQueueLocker.Unlock();
	        }
	
	        BBitmap* activeIcon = BTranslationUtils::GetBitmap(iconPath.c_str());
	        if (activeIcon != nullptr) {
	            if (activeIcon->IsValid()) {
	                fIconCache.push_back(activeIcon);
	            } else {
	                delete activeIcon;
	                activeIcon = nullptr;
	            }
	        }
	
	        std::string timeContextLabel = "Now: ";
	        if (targetDateStr != nullptr && std::strlen(targetDateStr) > 0) {
	            std::string rawDate(targetDateStr);
	            // Formats long strings like "2026-06-24" into a scannable "06-24" UI prefix
	            std::string shortDate = (rawDate.length() >= 10) ? rawDate.substr(5) : rawDate;
	            timeContextLabel = "On " + shortDate + ": ";
	        }
	
	        std::string displayLabel = item.guideNumber + " - " + item.guideName + " (" + timeContextLabel + item.nowPlaying + ") ";
	        
	        fChannelListView->AddItem(new ChannelListItem(displayLabel.c_str(), activeIcon));
	    }
	    std::fflush(stdout); 
	
	    // Initialize Messenger targeting the current window instance handler securely
	    if (gIconWindowMessenger == nullptr) {
	        gIconWindowMessenger = new BMessenger(this);
	    }
	
	    // Safely verify and spawn the low priority background icon loader thread
	    if (atomic_get(&gIconThreadRunning) == 0) {
	        gIconQueueLocker.Lock();
	        bool queueHasWork = !gIconDownloadQueue.empty();
	        gIconQueueLocker.Unlock();
	
	        if (queueHasWork) {
	            thread_id downloader = spawn_thread(SerialIconDownloaderThread, "SerialIconWorker", B_LOW_PRIORITY, NULL);
	            if (downloader >= B_OK) {
	                resume_thread(downloader);
	            }
	        }
	    }
	    
	    // Set a final status label message reflecting the chunk compilation context
	    std::string successStatus = "Status: Loaded guide data for " + targetDateOnly;
	    fStatusLabel->SetText(successStatus.c_str());
	}


	void RefreshScheduleListView() {
        fScheduleListView->MakeEmpty();
        gScheduleLocker.Lock();
        for (const auto& item : gScheduleList) {
            if (!item.processed) {
                long totalSeconds = std::atol(item.duration.c_str());
                long totalMinutes = totalSeconds / 60;
                
                std::string durText = "";
                if (totalMinutes >= 60) {
                    long hours = totalMinutes / 60;
                    long remainingMins = totalMinutes % 60;
                    
                    if (remainingMins > 0) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%ldh %ldm", hours, remainingMins);
                        durText = buf;
                    } else {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%ldh", hours); // "7200" will now correctly display as "2h"
                        durText = buf;
                    }
                } else {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%ldm", totalMinutes);
                    durText = buf;
                }
                
                std::string shortDate = (item.startDate.length() >= 10) ? item.startDate.substr(5) : item.startDate;
                
                // =========================================================================
                // INLINE STRING FORMATTER: SWAP FILE UNDERSCORES BACK TO SPACES
                // =========================================================================
                // Convert file underscores back into beautiful user-friendly spaces on-the-fly!
                BString readableTitle(item.showTitle.c_str());
                readableTitle.ReplaceAll("_", " ");
                
                if (readableTitle.IsEmpty()) {
                    readableTitle = "Live Stream";
                }

                // Appends the clean title string right onto the end of your formatted entry row line
                std::string entryLabel = shortDate + " @ " + item.startTime + " -> Ch " + 
                                         item.channel + " (" + durText + ") - " + readableTitle.String();
                // =========================================================================
                
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
    
    

public:
    virtual ~DVRWindow(); 
    bool QuitRequested() override;
	
    const std::vector<ChannelGuideItem>& GetLoadedChannels() const { 
        return fLoadedChannels; 
    }

    DVRWindow() : BWindow(BRect(150, 150, 1030, 625), "Haiku HDHomeRun DVR Scheduler", B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS) {
        LoadSchedulesFromDisk();
        
        thread_id updateThread = spawn_thread(BackgroundUpdateChecker, "UpdateCheckerThread", B_NORMAL_PRIORITY, this);
        if (updateThread >= 0) {
            resume_thread(updateThread);
        }

		fLastNetworkSyncTime = 0; 
		// 300,000,000 microseconds = 5 minutes
		fRefreshRunner = new BMessageRunner(BMessenger(this), 
		    new BMessage(MSG_PERIODIC_GUIDE_REFRESH), 
		    300000000LL, -1); 
		
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
        BMenu* optionsMenu = new BMenu("Options");                

        BMessage* msgNotifyOn = new BMessage(MSG_TOGGLE_NOTIFICATIONS);
        fNotifyOnItem = new BMenuItem("Enable Update Alerts", msgNotifyOn);        
        fNotifyOnItem->SetMarked(cfg.showUpdateNotifications);
        optionsMenu->AddItem(fNotifyOnItem);  
            
        optionsMenu->AddSeparatorItem();          
        
        BMessage* msgDebugOn = new BMessage(MSG_TOGGLE_DEBUG);
        fDebugOnItem = new BMenuItem("Enable Debug Mode", msgDebugOn);        
        fDebugOnItem->SetMarked(cfg.debugEnable);
        optionsMenu->AddItem(fDebugOnItem);  
            
        optionsMenu->AddSeparatorItem(); 
        
        BMessage* msgDlnaOn = new BMessage(MSG_TOGGLE_DLNA);
        BString labelString;

        if (cfg.dlnaEnable) {
            int32 boundPort = 8081; // Fallback default
            
            // Loop through the 9 fallback port tries to see which socket is listening
            for (int32 port = 8081; port <= 8090; port++) {
                int socketFd = socket(AF_INET, SOCK_STREAM, 0);
                if (socketFd >= 0) {
                    struct sockaddr_in addr;
                    std::memset(&addr, 0, sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(port);
                    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                    
                    // Set a very quick timeout so it doesn't hang the GUI boot sequence
                    struct timeval tv;
                    tv.tv_sec = 0;
                    tv.tv_usec = 10000; // 10ms
                    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    
                    // If connect returns 0, the server is actively running on this port!
                    if (connect(socketFd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                        boundPort = port;
                        close(socketFd);
                        break;
                    }
                    close(socketFd);
                }
            }
            labelString.SetToFormat("Enable Http & Dlna Server (Active Port: %d)", boundPort);
        } else {
            labelString.SetTo("Enable Http & Dlna Server [Disabled]");
        }

        fDlnaOnItem = new BMenuItem(labelString.String(), msgDlnaOn);        
        fDlnaOnItem->SetMarked(cfg.dlnaEnable);
        optionsMenu->AddItem(fDlnaOnItem);  

            
        optionsMenu->AddSeparatorItem();         

        BMessage* msgOpenGuide = new BMessage(MSG_OPEN_GUIDE);
        BMenuItem* guideItem = new BMenuItem("Open Guide...", msgOpenGuide);
        optionsMenu->AddItem(guideItem);
        
        optionsMenu->AddSeparatorItem();
		BMenuItem* firmwareItem = new BMenuItem("Check Tuner Firmware...", new BMessage(MSG_CHECK_FIRMWARE));
		optionsMenu->AddItem(firmwareItem);	
		
        optionsMenu->AddSeparatorItem();        
        
        BMessage* msgAbout = new BMessage(MSG_ABOUT_WINDOW);
        fAboutItem = new BMenuItem("About...", msgAbout);        
        optionsMenu->AddItem(fAboutItem);  
        
        
        // =========================================================================
        // DEFAULT PLAYER RADIO SELECTION SECTION
        // =========================================================================
            
        optionsMenu->AddSeparatorItem(); 
        
        BMenuItem* playerHeader = new BMenuItem("    --- Default Player ---", NULL);
        playerHeader->SetEnabled(false); 
        optionsMenu->AddItem(playerHeader);

        fPlayerMpvItem   = new BMenuItem("MPV", new BMessage(MSG_SET_PLAYER_MPV));
        fPlayerMediaItem = new BMenuItem("MediaPlayer", new BMessage(MSG_SET_PLAYER_MEDIAPLAYER));
        fPlayerVlcItem   = new BMenuItem("VLC", new BMessage(MSG_SET_PLAYER_VLC));
        // --- ADD hTV MENU ITEM ---
        fPlayerHtvItem   = new BMenuItem("hTV", new BMessage(MSG_SET_PLAYER_HTV));

        fPlayerMpvItem->SetMarked(cfg.defaultPlayer == "MPV" || cfg.defaultPlayer.empty());
        fPlayerMediaItem->SetMarked(cfg.defaultPlayer == "MediaPlayer");
        fPlayerVlcItem->SetMarked(cfg.defaultPlayer == "VLC");
        // --- MATCH CONFIG VALUE FOR CHECKMARK ---
        fPlayerHtvItem->SetMarked(cfg.defaultPlayer == "hTV");

        optionsMenu->AddItem(fPlayerMpvItem);
        optionsMenu->AddItem(fPlayerMediaItem);
        optionsMenu->AddItem(fPlayerVlcItem);
        // --- INSERT INTO MENU LIST ---
        optionsMenu->AddItem(fPlayerHtvItem);
        
        menuBar->AddItem(optionsMenu);
       

        fCurrentFilter = FILTER_ALL;
        BMenu* filterMenu = new BMenu("Filter");
        BMenuItem* itemAll = new BMenuItem("All Channels", new BMessage(MSG_FILTER_ALL));
        BMenuItem* itemHd  = new BMenuItem("HD Only",      new BMessage(MSG_FILTER_HD));
        BMenuItem* itemSd  = new BMenuItem("SD Only",      new BMessage(MSG_FILTER_SD));
        itemAll->SetMarked(true); 
        filterMenu->AddItem(itemAll);
        filterMenu->AddItem(itemHd);
        filterMenu->AddItem(itemSd);

        menuBar->AddItem(filterMenu);    

        AddChild(menuBar);

		BView* view = new BView(Bounds(), "MainView", B_FOLLOW_ALL, B_WILL_DRAW);
		
        view->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));  
        
              
		fTunerMenu = new BPopUpMenu("Select Tuner");
		std::vector<std::string> foundTuners = DiscoverAllTuners();
		
		if (foundTuners.empty()) {
		    // 1. Safe fallback for the selected IP string
		    fSelectedIp = ""; 
		    
		    // 2. Add a disabled visual placeholder to the menu
		    BMenuItem* disabledItem = new BMenuItem("No Tuners Found", nullptr);
		    disabledItem->SetEnabled(false);
		    fTunerMenu->AddItem(disabledItem);
		    
		    // 3. Disable the record button later in your constructor if needed
		    // fRecordButton->SetEnabled(false);
		
		} else {
		    // 4. Safe to access index 0 because we verified the vector isn't empty
		    fSelectedIp = foundTuners[0];        
		    
		    for (size_t i = 0; i < foundTuners.size(); i++) {
		        BMessage* msg = new BMessage(MSG_TUNER_SELECTED);
		        msg->AddString("ip", foundTuners[i].c_str());
		        
		        BMenuItem* item = new BMenuItem(foundTuners[i].c_str(), msg);
		        if (foundTuners[i] == fSelectedIp) {
		            item->SetMarked(true);
		        }
		        fTunerMenu->AddItem(item);
		    }
		}

       
        fDurationMenu = new BPopUpMenu("Select Duration");
        AddDurationItem("30 Minutes", "1800", true);  
        AddDurationItem("1 Hour",     "3600");
        AddDurationItem("1.5 Hours",  "5400");
        AddDurationItem("2 Hours",    "7200");        
        
        // =========================================================================
        // EXTENDED MOVIE & SPORTING EVENT DURATION OPTIONS
        // =========================================================================
        // Appends the missing high-duration tags using your custom helper function
        AddDurationItem("2.5 Hours",  "9000");
        AddDurationItem("3 Hours",     "10800");
        AddDurationItem("3.5 Hours",  "12600");
        AddDurationItem("4 Hours",     "14400");
        // =========================================================================

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
        std::time_t rawCurrentTime = std::time(nullptr);
        std::tm* localTimeStruct = std::localtime(&rawCurrentTime);

        char timeTextBuffer[16];
        std::strftime(timeTextBuffer, sizeof(timeTextBuffer), "%H:%M", localTimeStruct);

        fTimeInput = new BTextControl(BRect(20, 175, 260, 200), "time", "Start Time:", timeTextBuffer, NULL);
        fTimeInput->SetDivider(85.0);

        fTimeInput->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));    
        fTimeInput->TextView()->SetFontAndColor(&digitalFont, B_FONT_ALL, &digitalGreen);
        fTimeInput->TextView()->SetAlignment(B_ALIGN_CENTER);

 		// =========================================================================
        // TIME STEP INTERFACE CONTROLS (MINUS / PLUS FIXED REVERSED POSITIONING)
        // =========================================================================
        BButton* btnTimeDown = new BButton(BRect(270, 175, 295, 198), "time_down", "-", new BMessage(MSG_CLOCK_DOWN));
        BButton* btnTimeUp = new BButton(BRect(305, 175, 330, 198), "time_up", "+", new BMessage(MSG_CLOCK_UP));        
        
        // =========================================================================
        // LEFT COLUMN: ACTION INTERFACE TRIGGERS
        // =========================================================================
        fRecordButton = new BButton(BRect(20, 215, 170, 250), "record", "Start Recording", new BMessage(MSG_START_RECORDING));
        fStopButton = new BButton(BRect(180, 215, 330, 250), "stop", "Stop Recording", new BMessage(MSG_STOP_RECORDING));
        fStopButton->SetEnabled(false);
        
        BButton* btnOpenGuide = new BButton(BRect(20, 255, 170, 290), "open_guide", "Open Guide", new BMessage(MSG_OPEN_GUIDE));
        BButton* btnOpenRecordings = new BButton(BRect(180, 255, 330, 290), "open_recordings", "Open Recordings", new BMessage(MSG_VIEW_RECORDINGS));
        
        BButton* btnSearchGuide = new BButton(BRect(20, 300, 170, 335), "search_guide", "Search Guide", new BMessage(MSG_OPEN_SEARCH_POPUP));
        
        // Queue button shifted to the right half (X: 180 to 330)
        fScheduleButton = new BButton(BRect(180, 300, 330, 335), "schedule", "Queue Show", new BMessage(MSG_ADD_SCHEDULE));
        // =========================================================================

        if (fBrowseButton != nullptr) {
            fBrowseButton->MoveTo(20, 345);
            fBrowseButton->ResizeTo(310, 35);
        }
        
        fCountdownLabel = new BStringView(BRect(20, 395, 330, 420), "countdown", "Time Remaining: --:--:--");
        fStatusLabel = new BStringView(BRect(20, 440, 830, 465), "status", "Status: Idle");
        fStatusLabel->SetAlignment(B_ALIGN_LEFT);                       
                    
        // =========================================================================
        // RIGHT COLUMN: "QUICK VIEW" CONTAINER BBOX (FIXED CONTAINER HEIGHTS)
        // =========================================================================
        BBox* quickViewBox = new BBox(BRect(360, 25, 860, 225), "quick_view_box");
        quickViewBox->SetLabel("Quick View");
        quickViewBox->SetBorder(B_FANCY_BORDER);

		fChannelListView = new BListView(BRect(0, 0, 480, 180), "channel_list", B_SINGLE_SELECTION_LIST);
		fChannelListView->SetSelectionMessage(new BMessage(MSG_CHANNEL_CLICKED));
		fChannelListView->SetInvocationMessage(new BMessage(MSG_CHANNEL_DOUBLE_CLICKED));
		        
        fChannelScrollView = new BScrollView("scroll_channels", fChannelListView, B_FOLLOW_LEFT | B_FOLLOW_TOP, 0, false, true);
        
        fChannelScrollView->MoveTo(10, 20);
        fChannelScrollView->ResizeTo(480, 150);        
        quickViewBox->AddChild(fChannelScrollView);

        // =========================================================================
        // RIGHT COLUMN: "QUEUED SCHEDULES" CONTAINER BBOX
        // =========================================================================
        BBox* queuedSchedulesBox = new BBox(BRect(360, 240, 860, 365), "queued_schedules_box");
        queuedSchedulesBox->SetLabel("Queued Schedules (💡 Right Click to Delete )");
        queuedSchedulesBox->SetBorder(B_FANCY_BORDER);

        fScheduleListView = new ScheduleListView(BRect(0, 0, 480, 105), "schedule_list");
        
        // Wrap and map coordinates relative to the inner margin limits of queuedSchedulesBox
        fScheduleScrollView = new BScrollView("scroll_schedules", fScheduleListView, B_FOLLOW_LEFT | B_FOLLOW_TOP, 0, false, true);       
        fScheduleScrollView->MoveTo(10, 20);
        fScheduleScrollView->ResizeTo(480, 95);
        queuedSchedulesBox->AddChild(fScheduleScrollView);

        
        // =========================================================================
        // APPLICATION BACKEND SYSTEM CONSOLES (RECALIBRATED VERTICAL SPACING)
        // =========================================================================
        fRestartBackendButton = new BButton(BRect(730, 375, 860, 405), "restart_backend", "Restart Backend", new BMessage(MSG_RESTART_BACKEND));

        BBox* statusBox = new BBox(BRect(730, 415, 860, 445), "bebox_status_wrapper");
        statusBox->SetBorder(B_FANCY_BORDER); 

        fBackendStatusLabel = new BStringView(BRect(5, 5, 125, 25), "backend_status", "Connecting...");
        BFont monoFont(be_fixed_font);
        monoFont.SetSize(11.0);
        fBackendStatusLabel->SetFont(&monoFont);
        fBackendStatusLabel->SetAlignment(B_ALIGN_CENTER);
        statusBox->AddChild(fBackendStatusLabel);


        // =========================================================================
        // ASSEMBLY: APPEND STRUCTURAL OBJECTS INTO MAIN PANEL VIEW
        // =========================================================================
        // --- Left Column: Configuration Forms ---
        view->AddChild(fTunerSelector);
        view->AddChild(fChannelInput);
        view->AddChild(fDurationSelector);
        view->AddChild(fDateInput);         
        view->AddChild(fDateBrowseButton);   
        view->AddChild(fTimeInput);
        view->AddChild(btnTimeDown);
        view->AddChild(btnTimeUp);

        // --- Left Column: Action Triggers & Status ---
        view->AddChild(fRecordButton);
        view->AddChild(fStopButton);
        view->AddChild(btnOpenGuide);
        view->AddChild(btnOpenRecordings);
        view->AddChild(btnSearchGuide);
        view->AddChild(fScheduleButton);
        view->AddChild(fBrowseButton);
        view->AddChild(fStatusLabel);
        view->AddChild(fCountdownLabel);

        // --- Right Column: Structured BBox Frames & System Monitors ---
        view->AddChild(quickViewBox);
        view->AddChild(queuedSchedulesBox);
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


	void WindowActivated(bool active) {
	    BWindow::WindowActivated(active);
	    PostMessage(B_COLORS_UPDATED);
	}    
			
    void MessageReceived(BMessage* message) override {
        switch (message->what) {
        	
        	
        case MSG_TOGGLE_NOTIFICATIONS: {
            cfg.showUpdateNotifications = !cfg.showUpdateNotifications;            
            fNotifyOnItem->SetMarked(cfg.showUpdateNotifications);          
            SaveSchedulesToDisk();
            break;
        }
        
        case MSG_TOGGLE_DEBUG: {
            cfg.debugEnable = !cfg.debugEnable;            
            fDebugOnItem->SetMarked(cfg.debugEnable);          
            SaveSchedulesToDisk();
                            
            printf("[DEBUG_SYS] System logging runtime state mutated via UI: %s\n", 
                   cfg.debugEnable ? "ENABLED" : "DISABLED");
            break;
        }
     
       case MSG_ABOUT_WINDOW: {
                BString aboutText;
                aboutText <<  AppInfo::VERSION_STRING << "\n" 
                		  << "By Kris Beazley (ablyss)\n"
                		  << "Copyright 2026 The MIT License\n\n"

                          << "HaikuDVR is a native GUI digital video recorder application and " 
                          << "background scheduling service designed explicitly for Haiku.\n\n"
                          
                          << "Features:\n"
                          << "     * Sqlite Database\n"
                          << "     * Default Player Selection\n"
                          << "     * DLNA Server\n"
                          << "     * Online Update Checking\n"
                          << "     * And Much More!\n";

                BAlert* aboutAlert = new BAlert("About HaikuDVR", aboutText.String(), "OK", 
                                                nullptr, nullptr, B_WIDTH_AS_USUAL, B_INFO_ALERT);                
                aboutAlert->Go();
                break;
            }
     
        
        case MSG_TOGGLE_DLNA: {
            cfg.dlnaEnable = !cfg.dlnaEnable;            
            fDlnaOnItem->SetMarked(cfg.dlnaEnable);          
            SaveSchedulesToDisk();

            BAlert* alert = new BAlert("Warning",
                "Toggling the DLNA server requires restarting the backend service.\n\n"
                "Any current recordings in progress will be lost! Do you want to proceed?",
                "Cancel", "Proceed", nullptr, B_WIDTH_FROM_LABEL, B_WARNING_ALERT);
            
            int32 response = alert->Go();
            
            if (response == 0) {
                // User clicked "Cancel" -> Revert settings and menu state
                cfg.dlnaEnable = !cfg.dlnaEnable;            
                fDlnaOnItem->SetMarked(cfg.dlnaEnable);          
                SaveSchedulesToDisk();
                break; 
            }

            // --- FIXED: Provide visual menu label updates immediately on user action ---
            if (cfg.dlnaEnable) {
                fDlnaOnItem->SetLabel("Enable Http & Dlna Server [Restarting Backend...]");
            } else {
                fDlnaOnItem->SetLabel("Enable Http & Dlna Server [Disabled]");
            }

            // User clicked "Proceed" -> Programmatically notify the backend to apply new DLNA settings
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


        
         case MSG_TOGGLE_FULLSCREEN: {
            cfg.fullscreenEnable = !cfg.fullscreenEnable;            
            fFullscreenOnItem->SetMarked(cfg.fullscreenEnable);          
            SaveSchedulesToDisk();
            break;
        }

            
        case MSG_POLL_BACKEND: {
            LoadSchedulesFromDisk();
            RefreshScheduleListView();
            
            BMessenger serviceTarget("application/x-vnd.haikuhdhomerun-dvr");
            bool isRunning = serviceTarget.IsValid();            
            
            rgb_color activeGreen  = { 0, 160, 0, 255 };  
            rgb_color alertRed     = { 225, 0, 0, 255 };  
            rgb_color orangeNotice = { 230, 115, 0, 255 };

            if (isRunning) {
                fBackendStatusLabel->SetText("CONNECTED");
                fBackendStatusLabel->SetHighColor(activeGreen); 
            } else {

                BEntry serverBinary("/boot/system/servers/dvr_server");                
                if (serverBinary.Exists()) {
                    fBackendStatusLabel->SetText("REBOOT REQUIRED");
                    fBackendStatusLabel->SetHighColor(orangeNotice);
                } else {
                    fBackendStatusLabel->SetText("OFFLINE");
                    fBackendStatusLabel->SetHighColor(alertRed); 
                }
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
	


	
	     case MSG_DISK_SPACE_WARNING: {
	         int32 freeMB = 0;
	         if (message->FindInt32("free_mb", &freeMB) == B_OK) {
	             char warningMessage[128];
	             sprintf(warningMessage, "CRITICAL WARNING: Storage space running low! Only %" B_PRId32 " MB remaining in save directory.", freeMB);
	             
	             fStatusLabel->SetText(warningMessage);
	             
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
                 
                 fStatusLabel->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
                 fStatusLabel->Invalidate();
             }
             break;
         }

      case MSG_DATE_SELECTED: {
         const char* newDateString = nullptr;
         if (message->FindString("date_string", &newDateString) == B_OK) {
             
             // 1. Update the Main Scheduler Window's text field directly
             fDateInput->SetText(newDateString);
             FetchAndPopulateChannelList(newDateString);

             // =========================================================================
             // DIRECT WINDOW CROSS-POINTER SYNCHRONIZATION VIA FGUIDEWINDOW
             // =========================================================================
             // If the Guide Matrix Panel window is currently running open on screen
             if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
                 
                 // Look for a view named "date_input" inside the guide window frame context
                 BView* targetedView = fGuideWindow->FindView("date_input");
                 BTextControl* guideDateControl = dynamic_cast<BTextControl*>(targetedView);
                 
                 if (guideDateControl != nullptr) {
                     // Sync its text box to match the chosen calendar date string
                     guideDateControl->SetText(newDateString);
                 }
                 
                 // Forward the selection notice message so the guide window triggers its row redraws
                 BMessage refreshMessage(MSG_DATE_SELECTED);
                 fGuideWindow->PostMessage(&refreshMessage);
                 
                 // Handle timeline timeline header updates natively
                 BView* childHeader = fGuideWindow->FindView("timelineHeader");
                 if (childHeader != nullptr) {
                     BMessage syncHeaderMsg('UCLT');
                     if (fTimeInput != nullptr && fTimeInput->Text() != nullptr) {
                         syncHeaderMsg.AddString("time", fTimeInput->Text());
                     }
                     syncHeaderMsg.AddString("date", newDateString);
                     BMessenger(childHeader).SendMessage(&syncHeaderMsg);
                 }
                 
                 fGuideWindow->Unlock();
             }
             // =========================================================================

             BListView* realGuideList = dynamic_cast<BListView*>(FindView("guide_list_view"));             
             if (realGuideList != nullptr) {
                 realGuideList->MakeEmpty(); 
                 realGuideList->Invalidate(); 
             }

             std::time_t rawToday = std::time(nullptr);
             std::tm* localToday = std::localtime(&rawToday);
             char todayBuf[32];
             std::strftime(todayBuf, sizeof(todayBuf), "%Y-%m-%d", localToday);

             BStringView* timeHeaderLabel = dynamic_cast<BStringView*>(FindView("cur_time_head")); 
             if (timeHeaderLabel != nullptr) {
                 if (std::string(newDateString) == todayBuf) {
                     timeHeaderLabel->SetText("CURRENT TIME");
                 } else {
                     timeHeaderLabel->SetText("SELECTED TIME");
                 }
             }
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


	     case MSG_TUNER_SELECTED: {
	         const char* newIp;
	         if (message->FindString("ip", &newIp) == B_OK) {
	             fSelectedIp = newIp;
	             std::string statusMsg = "Selected Tuner: " + fSelectedIp;
	             fStatusLabel->SetText(statusMsg.c_str());
	             FetchAndPopulateChannelList();
	         }
	         
	       	 if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
			            BMessage refreshGuide(MSG_PERIODIC_GUIDE_REFRESH);
			            fGuideWindow->PostMessage(&refreshGuide);
			            fGuideWindow->Unlock();
	         }
	         
	         break;
	     }
     
         
     
     
         case MSG_OPEN_SEARCH_POPUP: {
                ProgramSearchWindow* searchEnginePopup = new ProgramSearchWindow(this);
                searchEnginePopup->Show();
                break;
            }


           case MSG_SEARCH_SELECTED: {
            BString foundTitle, foundDesc, targetChannelId, friendlyChannelNum;
            int64 targetEpoch = 0;
            
            if (message->FindString("title", &foundTitle) == B_OK) {
                // Populates manual text inputs if needed
            }
            
            if (message->FindString("description", &foundDesc) == B_OK) {
                if (fStatusLabel != nullptr) {
                    BString statusDisplay;
                    statusDisplay.SetToFormat("Selected Guide Show: %s | %s", foundTitle.String(), foundDesc.String());
                    fStatusLabel->SetText(statusDisplay.String());
                }
            }

            // INSTANT SNAPPING AND VIEW ALIGNMENT CONTROLLER
            if (message->FindString("channel_id", &targetChannelId) == B_OK &&
                message->FindInt64("start_epoch", &targetEpoch) == B_OK &&
                message->FindString("channel", &friendlyChannelNum) == B_OK) {

                // 1. VERTICAL SCROLL (MAIN WINDOW): Snap list selection to the channel row index
                if (fChannelListView != nullptr) {
                    int32 rowCount = fChannelListView->CountItems();
                    for (int32 i = 0; i < rowCount; i++) {
                        ChannelListItem* channelRow = (ChannelListItem*)fChannelListView->ItemAt(i);
                        if (channelRow != nullptr) {
                            BString checkText(channelRow->textDisplay.c_str());
                            BString searchMatchPattern;
                            searchMatchPattern.SetToFormat("%s -", friendlyChannelNum.String());

                            if (checkText.FindFirst(searchMatchPattern) != B_ERROR || checkText.StartsWith(friendlyChannelNum)) {
                                fChannelListView->Select(i);
                                fChannelListView->ScrollToSelection();
                                break;
                            }
                        }
                    }
                }

                // 2. HORIZONTAL TIMELINE UPDATE: Sync grid timeline coordinates
                std::time_t epochTime = (std::time_t)targetEpoch;
                std::tm* localTimeBox = std::localtime(&epochTime);
                
                if (localTimeBox != nullptr) {
                    char timeBuf[32] = {0};
                    char dateBuf[32] = {0};
                    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M", localTimeBox);
                    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localTimeBox);
                    
                    if (fTimeInput != nullptr) fTimeInput->SetText(timeBuf);
                    if (fDateInput != nullptr) fDateInput->SetText(dateBuf);
                    if (fChannelInput != nullptr) fChannelInput->SetText(friendlyChannelNum.String());

                    // DURATION DROPDOWN MANAGER ENGINE
                    int64 targetEndEpoch = 0;
                    int32 showMinutes = 30; // Reliable fallback metric bounds
                    if (message->FindInt64("end_epoch", &targetEndEpoch) == B_OK && targetEndEpoch > targetEpoch) {
                        showMinutes = (int32)((targetEndEpoch - targetEpoch) / 60);
                    }

                    if (fDurationMenu != nullptr && showMinutes > 0) {
                        BString targetMenuLabel;
                        if (showMinutes < 60) {
                            targetMenuLabel.SetToFormat("%d Minutes", showMinutes);
                        } else {
                            double hoursValue = (double)showMinutes / 60.0;
                            if (showMinutes % 60 == 0) {
                                targetMenuLabel.SetToFormat("%.0f Hour%s", hoursValue, (hoursValue > 1.0 ? "s" : ""));
                            } else {
                                targetMenuLabel.SetToFormat("%.1f Hours", hoursValue);
                            }
                        }

                        BMenuItem* targetMenuItem = fDurationMenu->FindItem(targetMenuLabel.String());
                        if (targetMenuItem != nullptr) {
                            targetMenuItem->SetMarked(true);
                            if (fDurationSelector != nullptr && fDurationSelector->MenuBar() != nullptr) {
                                BMenuBar* menuBarContainer = fDurationSelector->MenuBar();
                                if (menuBarContainer->CountItems() > 0) {
                                    BMenuItem* activeFieldItem = menuBarContainer->ItemAt(0);
                                    if (activeFieldItem != nullptr) {
                                        activeFieldItem->SetLabel(targetMenuLabel.String());
                                    }
                                }
                                fDurationSelector->Invalidate();
                            }
                        }

                        BString secondsConverter;
                        secondsConverter.SetToFormat("%d", showMinutes * 60);
                        fSelectedDurationSeconds = secondsConverter.String();
                    }

                    // 3. CROSS-WINDOW SATELLITE COUPLING: Synchronize timelines and scroll grids
                    FetchAndPopulateChannelList(dateBuf);

                    if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
                        RealTVGuideWindow* guideWin = dynamic_cast<RealTVGuideWindow*>(fGuideWindow);

                        BView* childHeader = fGuideWindow->FindView("timelineHeader");
                        if (childHeader != nullptr) {
                            TimelineHeaderView* headerView = dynamic_cast<TimelineHeaderView*>(childHeader);
                            if (headerView != nullptr) {
                                headerView->fCachedSelectedTime = timeBuf;
                                headerView->fCachedSelectedDate = dateBuf;
                            }
                        }

                        // Rebuild guide database row data cell arrays
                        BMessage refreshGuide(MSG_PERIODIC_GUIDE_REFRESH);
                        fGuideWindow->PostMessage(&refreshGuide);
                        
                        if (childHeader != nullptr) {
                            BMessage syncHeaderMsg('UCLT');
                            syncHeaderMsg.AddString("time", timeBuf);
                            syncHeaderMsg.AddString("date", dateBuf);
                            BMessenger(childHeader).SendMessage(&syncHeaderMsg);
                        }

                        // Coordinate grid viewport scrolling vertically down to the target channel index row
                        if (guideWin != nullptr && guideWin->fMainChannelListView != nullptr) {
                            int32 guideRowCount = guideWin->fMainChannelListView->CountItems();
                            for (int32 k = 0; k < guideRowCount; k++) {
                                ChannelListItem* guideRowItem = (ChannelListItem*)guideWin->fMainChannelListView->ItemAt(k);
                                if (guideRowItem != nullptr) {
                                    BString checkGuideText(guideRowItem->textDisplay.c_str());
                                    BString searchMatchPattern;
                                    searchMatchPattern.SetToFormat("%s -", friendlyChannelNum.String());

                                    if (checkGuideText.FindFirst(searchMatchPattern) != B_ERROR || checkGuideText.StartsWith(friendlyChannelNum)) {
                                        guideWin->fMainChannelListView->Select(k);
                                        guideWin->fMainChannelListView->ScrollToSelection();
                                        
                                        if (guideWin->fContainerList != nullptr) {
                                            guideWin->fContainerList->Select(k);
                                            guideWin->fContainerList->ScrollToSelection();
                                        }
                                        break;
                                    }
                                }
                            }
                        }

                        if (childHeader != nullptr) {
                            childHeader->Invalidate(); 
                            if (childHeader->Parent() != nullptr) {
                                childHeader->Parent()->Invalidate();
                            }
                        }
                        
                        fGuideWindow->Unlock();
                    }
                }
            }
            break;
        }

 
         case MSG_FILTER_ALL:
         case MSG_FILTER_HD:
         case MSG_FILTER_SD: {
             BMenuBar* menuBar = dynamic_cast<BMenuBar*>(FindView("top_menubar"));
             if (menuBar) {
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
             
             if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
		            BMessage refreshGuide(MSG_PERIODIC_GUIDE_REFRESH);
		            fGuideWindow->PostMessage(&refreshGuide);
		            fGuideWindow->Unlock();
         	 }
         	 
             break;
         }


          case MSG_REFRESH_CHANNEL_LIST_ICONS: {
            int32 totalUiItems = fChannelListView ? fChannelListView->CountItems() : 0;

            for (int32 idx = 0; idx < totalUiItems; idx++) {
                if (idx >= (int32)fLoadedChannels.size()) break;

                const auto& item = fLoadedChannels[idx];
                std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + item.guideName + ".png";
                
                std::ifstream checkFile(iconPath.c_str());
                bool existsOnDisk = checkFile.good();
                checkFile.close();

                if (existsOnDisk) {
                    ChannelListItem* rowWidget = static_cast<ChannelListItem*>(fChannelListView->ItemAt(idx));
                    
                    if (rowWidget != nullptr && rowWidget->channelIcon == nullptr) {
                        BBitmap* freshIcon = BTranslationUtils::GetBitmap(iconPath.c_str());
                        
                        if (freshIcon != nullptr && freshIcon->IsValid()) {
                            fIconCache.push_back(freshIcon); 
                            rowWidget->channelIcon = freshIcon; 
                            fChannelListView->InvalidateItem(idx); 
                        } else if (freshIcon) {
                            delete freshIcon;
                        }
                    }
                }
            }

            if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
                fGuideWindow->PostMessage(MSG_REFRESH_CHANNEL_LIST_ICONS);
                fGuideWindow->Unlock();
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
       			  
       			 BListView* realGuideList = dynamic_cast<BListView*>(FindView("guide_list_view"));
	             if (realGuideList != nullptr) {
	                 realGuideList->Invalidate(); 
	             }
	             
	             fStatusLabel->SetText("Status: Schedule deleted.");
	         }
	         break;
	     }
     
	     case MSG_REFRESH_SCHEDULES:
	         RefreshScheduleListView();
	         break;
	         

		case MSG_CHANNEL_DOUBLE_CLICKED:
		{
		    int32 selection = fChannelListView->CurrentSelection();
		    if (selection >= 0) {
		        GuideListRowItem* item = static_cast<GuideListRowItem*>(
		            fChannelListView->ItemAt(selection)
		        );
		
		        if (item != nullptr) {
		            // FIX: Force a fresh copy using .String() so the original item label isn't modified
		            BString cleanNumberOnly;
		            cleanNumberOnly.SetTo(item->fData.channelLabel.String());
		
		            int32 sliceIndex = cleanNumberOnly.FindFirst(" ");
		            if (sliceIndex != B_ERROR) {
		                cleanNumberOnly.Truncate(sliceIndex);
		            }
		            cleanNumberOnly.Trim();
		
		            BMessage playMsg(MSG_PLAY_IN_MPV);
		            playMsg.AddString("numeric_channel", cleanNumberOnly.String());
		            
		            // Post directly to this window:
		            PostMessage(&playMsg); 
		
		            // Redraw the item to clear any remaining highlight artifacts
		            fChannelListView->InvalidateItem(selection);
		        }
		    }
		    break;
		}
		
		
         case MSG_CHANNEL_CLICKED: {
             int32 selection = fChannelListView->CurrentSelection();
             if (selection >= 0 && (size_t)selection < fLoadedChannels.size()) {
                 const auto& channel = fLoadedChannels[selection];
                 
                 fChannelInput->SetText(channel.guideNumber.c_str());
                 
                 // =========================================================================
                 // SINGLE SELECTED SHOW INFORMATION TRACKER
                 // =========================================================================
                 BString cleanTitle(channel.nowPlaying.c_str());
                 BString cleanDesc(channel.nowPlayingDescription.c_str());
                 
                 // Decode HTML/XML escape fragments into pristine plain text characters
                 auto CleanTextStrings = [](BString& s) {
                     s.ReplaceAll("&amp;",  "&");
                     s.ReplaceAll("&quot;", "\"");
                     s.ReplaceAll("&apos;", "'");
                     s.ReplaceAll("&#39;",  "'");
                     s.ReplaceAll("&lt;",   "<");
                     s.ReplaceAll("&gt;",   ">");
                     s.Trim();
                 };
                 
                 CleanTextStrings(cleanTitle);
                 CleanTextStrings(cleanDesc);

                 if (cleanTitle.IsEmpty()) {
                     cleanTitle = "Live Stream / Unknown Program";
                 }
                 if (cleanDesc.IsEmpty()) {
                     cleanDesc = "No further program description text details provided by broadcaster.";
                 }

                 // Generate a unified layout display string: "Selected: [Until 10:00 PM] Title — Description"
                 BString finalCleanPreview;
                 finalCleanPreview.SetToFormat("Selected Channel %s: [Until %s] %s — %s",
                     channel.guideNumber.c_str(),
                     channel.nowPlayingEndTimeStr.c_str(),
                     cleanTitle.String(),
                     cleanDesc.String()
                 );
                 
                 // =========================================================================
                 // AUTOMATIC DURATION DROPDOWN UPDATE ENGINE
                 // =========================================================================
                 if (fDurationMenu != nullptr) {
                     // 1. Fall back safely to 30 minutes if the guide data variable is missing
                     int32 showMinutes = channel.nowPlayingDurationMinutes;
                     if (showMinutes <= 0) {
                         showMinutes = 30; 
                     }

                     // 2. Generate the exact textual menu item string label to search for
                     BString targetMenuLabel;
                     if (showMinutes < 60) {
                         targetMenuLabel.SetToFormat("%d Minutes", showMinutes);
                     } else {
                         // Formats clean whole/fractional hour strings dynamically (e.g. 60m -> "1 Hour", 90m -> "1.5 Hours")
                         double hoursValue = (double)showMinutes / 60.0;
                         if (showMinutes % 60 == 0) {
                             targetMenuLabel.SetToFormat("%.0f Hour%s", hoursValue, (hoursValue > 1.0 ? "s" : ""));
                         } else {
                             targetMenuLabel.SetToFormat("%.1f Hours", hoursValue);
                         }
                     }

                     // 3. Scan and find the exact BMenuItem matching the string layout
                     BMenuItem* targetMenuItem = fDurationMenu->FindItem(targetMenuLabel.String());
                     if (targetMenuItem != nullptr) {
                         targetMenuItem->SetMarked(true); // Highlights the item inside the menu pop-up container
                     }

                     // 4. SYNCHRONIZE BACKGROUND RECORDING STATE ARRAYS
                     // Used .String() to match native Haiku BString conventions
                     BString secondsConverter;
                     secondsConverter.SetToFormat("%d", showMinutes * 60);
                     fSelectedDurationSeconds = secondsConverter.String(); // Updates std::string context properties safely
                 }
                 // =========================================================================
                 
                 fStatusLabel->SetText(finalCleanPreview.String());
                 fStatusLabel->SetFont(be_bold_font);
                 fStatusLabel->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
                 fStatusLabel->Invalidate();
             }
             break;
         }


	
		case MSG_RESTART_BACKEND:
		{			
			// Native Haiku Alert Popup with corrected button layout enum
            BAlert* alert = new BAlert("Warning",
                "Restarting the backend service.\n\n"
                "Any current recordings in progress will be stopped! Do you want to proceed?",
                "Cancel", "Proceed", nullptr, B_WIDTH_FROM_LABEL, B_WARNING_ALERT);
            
            // Go() blocks until the user interacts with the popup window
            int32 response = alert->Go();
            
            if (response == 0) {               
                break; 
            }
            
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

            BWindow* existingGuide = be_app->WindowAt(0);
            int32 winIdx = 0;
            bool foundMinimized = false;
            
            while (existingGuide != nullptr) {
                if (strcmp(existingGuide->Title(), "Interactive TV Guide Matrix") == 0) {
                    if (existingGuide->Lock()) {
                        existingGuide->Minimize(false); 
                        existingGuide->Activate(true);  
                        existingGuide->Unlock();
                    }
                    fGuideWindow = existingGuide;
                    foundMinimized = true;
                    break;
                }
                winIdx++;
                existingGuide = be_app->WindowAt(winIdx);
            }

            if (!foundMinimized) {
                BRect guideFrame(100, 100, 1050, 700);
                fGuideWindow = new RealTVGuideWindow(guideFrame, fLoadedChannels, fChannelListView, this);
                fGuideWindow->Show();
            }
            break;
        }

       


     case MSG_PLAY_IN_MPV: {
         BString numericChannel;
         if (message->FindString("numeric_channel", &numericChannel) == B_OK) {
             
             BString currentIp(fSelectedIp.c_str());
             if (currentIp.IsEmpty()) {
                 currentIp = "127.0.0.1";
             }

             
             BString streamUrl;
             streamUrl.SetToFormat("http://%s:5004/auto/v%s", currentIp.String(), numericChannel.String());
             

             if (cfg.defaultPlayer == "MediaPlayer") {
                 if (cfg.debugEnable) {
                     printf("[DEBUG PLAYER] Dispatching stream via B_ARGV_RECEIVED Roster to MediaPlayer: %s\n", streamUrl.String());
                 }

                 BMessage launchMessage(B_ARGV_RECEIVED);
                 launchMessage.AddString("argv", "/boot/system/apps/MediaPlayer");
                 launchMessage.AddString("argv", streamUrl.String());
                 launchMessage.AddInt32("argc", 2);

                 status_t launchResult = be_roster->Launch("application/x-vnd.Haiku-MediaPlayer", &launchMessage);
                 if (launchResult != B_OK) {
                     BString errorStatus = "Playback Error: Could not launch MediaPlayer. Status: ";
                     errorStatus << launchResult;
                     fStatusLabel->SetText(errorStatus.String());
                 } else {
                     BString playingNotification = "Streaming Live via MediaPlayer: Channel ";
                     playingNotification << numericChannel;
                     fStatusLabel->SetText(playingNotification.String());
                 }
             } 

             else {
                 const char* binaryPath = "/boot/system/bin/mpv";
                 const char* playerName = "mpv";
                 
                 if (cfg.defaultPlayer == "VLC") {
                     binaryPath = "/boot/system/bin/vlc"; 
                     playerName = "VLC";
                 }
                 else if (cfg.defaultPlayer == "hTV") {
                     binaryPath = "/boot/system/bin/hTV";
                     playerName = "hTV";
                 }

                 if (cfg.debugEnable) {
                     printf("[DEBUG PLAYER] Forking independent process for %s: %s\n", playerName, streamUrl.String());
                 }

                 pid_t processId = fork();
                 if (processId < 0) {
                     fStatusLabel->SetText("Playback Error: Failed to fork execution thread.");
                 } 
                 else if (processId == 0) {
         
                     char* playerArgs[3];
                     playerArgs[0] = (char*)binaryPath;
                     playerArgs[1] = (char*)streamUrl.String();
                     playerArgs[2] = nullptr; 
                     
                     execv(playerArgs[0], playerArgs);
                     _exit(1); 
                 } 
                 else {
                     BString playingNotification = "Streaming Live via ";
                     playingNotification << playerName << ": Channel " << numericChannel;
                     fStatusLabel->SetText(playingNotification.String());
                 }
             }
         }
         break;
     }



 case MSG_PREFILL_RECORD_SCHEDULE: {
        BString showTitle, startTime, channelLabel, numericSubchannel;
        int32 durationMinutes = 0;
        
        if (message->FindString("show_title", &showTitle) == B_OK &&
            message->FindString("start_time", &startTime) == B_OK &&
            message->FindString("numeric_subchannel", &numericSubchannel) == B_OK &&
            message->FindInt32("duration_minutes", &durationMinutes) == B_OK) {
            
            BString processedTime = startTime;

            if (fTimeInput != nullptr) {
                int32 hour = 0;
                int32 minute = 0;
                char ampm[16] = {0};

                if (sscanf(startTime.String(), "%d:%d %15s", &hour, &minute, ampm) >= 2) {
                    BString ampmStr(ampm);
                    if (ampmStr.IFindFirst("PM") != B_ERROR && hour < 12) {
                        hour += 12;
                    } else if (ampmStr.IFindFirst("AM") != B_ERROR && hour == 12) {
                        hour = 0;
                    }
                    processedTime.SetToFormat("%02d:%02d", hour, minute);
                } 
                else if (sscanf(startTime.String(), "%d:%d", &hour, &minute) == 2) {
                    processedTime.SetToFormat("%02d:%02d", hour, minute);
                }
                else if (startTime == "LIVE NOW") {
                    std::time_t raw = std::time(nullptr);
                    std::tm* loc = std::localtime(&raw);

                    int32 roundedMin = (loc->tm_min < 30) ? 0 : 30;
                    processedTime.SetToFormat("%02d:%02d", loc->tm_hour, roundedMin);
                }
                
                fTimeInput->SetText(processedTime.String());
            }

            if (fDurationMenu != nullptr && durationMinutes > 0) {
                BString targetDurationLabel;
                targetDurationLabel << durationMinutes << " Mins";
                BMenuItem* matchingItem = fDurationMenu->FindItem(targetDurationLabel.String());
                
                BString secondsStr;
                secondsStr << (durationMinutes * 60);

                if (matchingItem != nullptr) {
                    matchingItem->SetMarked(true);
                    
                    BMessage* itemMsg = matchingItem->Message();
                    if (itemMsg != nullptr) {
                        itemMsg->RemoveName("seconds"); 
                        itemMsg->AddString("seconds", secondsStr.String());
                    }
                } else {
                    BMessage* customDurationMsg = new BMessage(MSG_DURATION_SELECTED); 
                    customDurationMsg->AddInt32("minutes", durationMinutes);
                    customDurationMsg->AddString("seconds", secondsStr.String());
                    
                    BMenuItem* dynamicItem = new BMenuItem(targetDurationLabel.String(), customDurationMsg);
                    fDurationMenu->AddItem(dynamicItem);
                    dynamicItem->SetMarked(true);
                }
                
                fSelectedDurationSeconds = secondsStr.String();
                
                BMenuField* parentField = dynamic_cast<BMenuField*>(fDurationMenu->Supermenu());
                if (parentField != nullptr && parentField->MenuItem() != nullptr) {
                    parentField->MenuItem()->SetLabel(targetDurationLabel.String());
                }
            }

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
            
            BString trackingNotice = "Selected Lineup: ";
            trackingNotice << showTitle;
            fStatusLabel->SetText(trackingNotice.String());

            bool autoCommit = false;
            if (message->FindBool("auto_commit_queue", &autoCommit) == B_OK && autoCommit) {
                std::string rawTime = processedTime.String();

                if (rawTime.length() == 4 && rawTime.find(':') == std::string::npos) {
                    rawTime.insert(2, ":");
                }

                if (fTimeInput != nullptr) {
                    fTimeInput->SetText(rawTime.c_str());
                }
                
                if (fSelectedDurationSeconds.empty() || fSelectedDurationSeconds == "0ss" || fSelectedDurationSeconds == "0s") {
                    if (fDurationMenu != nullptr && fDurationMenu->FindMarked() != nullptr) {
                        BMenuItem* markedDuration = fDurationMenu->FindMarked();
                        BMessage* durMsg = markedDuration->Message();
                        const char* menuSecs = nullptr;
                        
                        if (durMsg != nullptr && durMsg->FindString("seconds", &menuSecs) == B_OK) {
                            fSelectedDurationSeconds = menuSecs;
                        } else {
                            int32 parsedMins = 30;
                            if (sscanf(markedDuration->Label(), "%d", &parsedMins) == 1) {
                                BString fallbackSecs;
                                fallbackSecs << (parsedMins * 60);
                                fSelectedDurationSeconds = fallbackSecs.String();
                            }
                        }
                    }
                }

                // Parse out trailing 's' modifiers if any existed inside fSelectedDurationSeconds
                size_t sPos = fSelectedDurationSeconds.find('s');
                if (sPos != std::string::npos) {
                    fSelectedDurationSeconds = fSelectedDurationSeconds.substr(0, sPos); 
                }

                // =========================================================================
                //  UNIFIED COUPLING FIX: AUTO-COMMIT DATE CALCULATION SYNC ENGINE
                // =========================================================================
                BString finalizedRecordDate = "";
                BString incomingComputedDateToken;
                
                // Prioritize the verified target date pre-computed by our grid selection handler
                if (message->FindString("computed_target_date", &incomingComputedDateToken) == B_OK && !incomingComputedDateToken.IsEmpty()) {
                    finalizedRecordDate = incomingComputedDateToken;
                    if (fDateInput != nullptr) {
                        fDateInput->SetText(incomingComputedDateToken.String()); // Synchronize UI box
                    }
                } 
                // Fallback approach using message flags if string extraction was bypassed
                else {
                    finalizedRecordDate = (fDateInput != nullptr) ? fDateInput->Text() : "";
                    bool advanceCalendarDayFlag = false;
                    if (message->FindBool("advance_calendar_day", &advanceCalendarDayFlag) == B_OK && advanceCalendarDayFlag) {
                        int parsedYear = 2026, parsedMonth = 6, parsedDay = 25;
                        if (std::sscanf(finalizedRecordDate.String(), "%d-%d-%d", &parsedYear, &parsedMonth, &parsedDay) == 3) {
                            std::tm rolloverTimeBox = {0};
                            rolloverTimeBox.tm_year = parsedYear - 1900;
                            rolloverTimeBox.tm_mon  = parsedMonth - 1;
                            rolloverTimeBox.tm_mday = parsedDay + 1; // Bump calendar day forward explicitly
                            rolloverTimeBox.tm_hour = 12;
                            rolloverTimeBox.tm_isdst = -1;
                            
                            std::time_t normalizedFutureEpoch = std::mktime(&rolloverTimeBox);
                            if (normalizedFutureEpoch != (std::time_t)-1) {
                                std::tm* safeFutureTm = std::localtime(&normalizedFutureEpoch);
                                char rebalancedDateBuf[32];
                                std::strftime(rebalancedDateBuf, sizeof(rebalancedDateBuf), "%Y-%m-%d", safeFutureTm);
                                finalizedRecordDate = rebalancedDateBuf;
                                
                                if (fDateInput != nullptr) {
                                    fDateInput->SetText(rebalancedDateBuf);
                                }
                            }
                        }
                    }
                }
                // =========================================================================

                ScheduleItem item;
 				item.startDate = finalizedRecordDate.String(); // Clean tomorrow target applied safely
					item.startTime = rawTime;
					item.channel = fChannelInput->Text();
					item.duration = fSelectedDurationSeconds;
					item.processed = false;
					
                // SANITIZE TITLE ENTITIES (NO UNDERSCORES)
                BString cleanTitle(showTitle);
                cleanTitle.Trim();
                cleanTitle.ReplaceAll("&amp;",  "&");
                cleanTitle.ReplaceAll("&quot;", "\"");
                cleanTitle.ReplaceAll("&apos;", "'");
                item.showTitle = cleanTitle.IsEmpty() ? "Live Stream" : cleanTitle.String();

					
				// SANITIZE DESCRIPTION ENTITIES COMPLETELY BEFORE DISK ENTRY
                BString showDesc;
                if (message->FindString("show_description", &showDesc) == B_OK || 
                    message->FindString("description", &showDesc) == B_OK) {
                    showDesc.Trim();
                    showDesc.ReplaceAll("&quot;", "\""); 
                    showDesc.ReplaceAll("&amp;",  "&");
                    showDesc.ReplaceAll("&apos;", "'");
                    showDesc.ReplaceAll("&lt;",   "<");
                    showDesc.ReplaceAll("&gt;",   ">");
                    showDesc.Trim();
                    item.showDescription = showDesc.IsEmpty() ? "No description available." : showDesc.String();
                } else {
                    item.showDescription = "No description available.";
                }

				
				// Compute epoch metrics using our safe top-level math converter utility
				item.durationSec = std::atoll(item.duration.c_str());
				item.epochStart = CalculateEpoch(item.startDate, item.startTime);
				BMenuItem* markedTuner = fTunerMenu->FindMarked();
					if (markedTuner != nullptr) {
						item.tunerIp = markedTuner->Label();
						} else {
						item.tunerIp = fSelectedIp;
				}
					if (!channelLabel.IsEmpty()) {
						item.channelLabel = channelLabel.String();
							} else {
						item.channelLabel = "Ch_" + item.channel;
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
         
        // Format missing colon automatically if typed as a raw 4-digit number
        if (rawTime.length() == 4 && rawTime.find(':') == std::string::npos) {
            rawTime.insert(2, ":");
            fTimeInput->SetText(rawTime.c_str());
        }
         
        ScheduleItem item;
        item.startDate = fDateInput->Text(); 
        item.startTime = fTimeInput->Text();
        item.channel = fChannelInput->Text();
        item.duration = fSelectedDurationSeconds; 
        item.processed = false;

        // Compute absolute timestamps for the recording background worker engine
        item.durationSec = std::atoll(item.duration.c_str());
        item.epochStart  = CalculateEpoch(item.startDate, item.startTime);

        // =========================================================================
        // 1. EXTRACT PROGRAM AND DESCRIPTION DIRECTLY FROM THE LOADED CHANNELS CACHE
        // =========================================================================
        BString extractedTitle;
        BString extractedDesc;

        if (fChannelListView != nullptr) {
            int32 selectedIndex = fChannelListView->CurrentSelection();
            
            // Safe bounds check against your actual channel data layout vector
            if (selectedIndex >= 0 && (size_t)selectedIndex < fLoadedChannels.size()) {
                const ChannelGuideItem& activeChannelData = fLoadedChannels[selectedIndex];
                
                extractedTitle = activeChannelData.nowPlaying.c_str();
                extractedDesc  = activeChannelData.nowPlayingDescription.c_str();
            }
        }

        // Status bar text-parsing fallback routine if nothing was chosen in the list box
        if (extractedTitle.IsEmpty() && fStatusLabel != nullptr) {
            BString fullStatus = fStatusLabel->Text();
            
            if (!fullStatus.StartsWith("Status:")) {
                int32 prefixIndex = fullStatus.FindFirst(" - ");
                if (prefixIndex != B_ERROR) {
                    fullStatus.CopyInto(extractedTitle, prefixIndex + 3, fullStatus.Length());
                } else {
                    extractedTitle = fullStatus;
                    extractedTitle.ReplaceFirst("Selected Lineup: ", "");
                }

                int32 pipeIndex = extractedTitle.FindFirst("|");
                if (pipeIndex != B_ERROR) {
                    extractedTitle.Truncate(pipeIndex);
                }

                int32 closeBracketIndex = extractedTitle.FindFirst("]");
                if (closeBracketIndex != B_ERROR) {
                    BString temp;
                    extractedTitle.CopyInto(temp, closeBracketIndex + 1, extractedTitle.Length());
                    extractedTitle = temp;
                }
            }
        }

        // =========================================================================
        // 2. SANITIZE FIELDS INDEPENDENTLY (No global underscore replacements!)
        // =========================================================================
        extractedTitle.Trim();
        extractedTitle.ReplaceAll("&amp;",  "&");
        extractedTitle.ReplaceAll("&quot;", "\"");
        extractedTitle.ReplaceAll("&apos;", "'");
        extractedTitle.Trim();

        // FIXED: Clean out raw XML/HTML entities from description tags before writing to disk
        extractedDesc.Trim();
        extractedDesc.ReplaceAll("&quot;", "\"");
        extractedDesc.ReplaceAll("&amp;",  "&");
        extractedDesc.ReplaceAll("&apos;", "'");
        extractedDesc.ReplaceAll("&lt;",   "<");
        extractedDesc.ReplaceAll("&gt;",   ">");
        extractedDesc.Trim();

        // Strip out legacy manual tag fragments completely
        extractedTitle.ReplaceAll("Manual_Recording_", "");
        extractedTitle.ReplaceAll("Manual_Recording", "");
        extractedTitle.Trim();

        // Assign clean values natively to data types to match web configurations
        if (extractedTitle.IsEmpty()) {
            item.showTitle = "Live Stream";
        } else {
            item.showTitle = extractedTitle.String(); // e.g. "American Ninja Warrior"
        }

        if (extractedDesc.IsEmpty()) {
            item.showDescription = "No description available.";
        } else {
            item.showDescription = extractedDesc.String(); // Pristine text summary field passed safely
        }

        // =========================================================================
        // 3. TUNER MAPPING & METRIC COMMIT PIPELINES (CLEANED)
        // =========================================================================
        BMenuItem* markedTuner = fTunerMenu->FindMarked();
        if (markedTuner != nullptr) {
            item.tunerIp = markedTuner->Label();
        } else {
            item.tunerIp = fSelectedIp;
        }
         
        // Synchronize and write data safely across threads
        gScheduleLocker.Lock();
        gScheduleList.push_back(item);
        gScheduleLocker.Unlock();
         
        SaveSchedulesToDisk(); // Flushes slimmed down json schema straight to disk
        RefreshScheduleListView(); 

        std::string confMsg = "Queued for " + item.startTime + " (Ch " + item.channel + ")";
        fStatusLabel->SetText(confMsg.c_str());
        break;
    }





     case MSG_START_RECORDING: {
         const char* targetChannel = fChannelInput->Text();
         
         // Used .c_str() to match std::string object data types natively
         std::string targetDuration = fSelectedDurationSeconds.c_str();

         const char* forcedChannel = nullptr;
         const char* forcedDuration = nullptr;
         if (message->FindString("forced_channel", &forcedChannel) == B_OK) targetChannel = forcedChannel;
         if (message->FindString("forced_duration", &forcedDuration) == B_OK) targetDuration = forcedDuration;
         
         bool tunerAcquired = false;
         RecordingConfig* config = new RecordingConfig();
         config->windowMessenger = BMessenger(this);
         config->channel = targetChannel;
         config->duration = targetDuration;

        ScheduleItem item;
        item.startDate = fDateInput->Text(); 
        item.startTime = fTimeInput->Text();
        
        item.channel = targetChannel;
        item.duration = targetDuration; 
        item.processed = false;

        item.durationSec = std::atoll(item.duration.c_str());
        item.epochStart  = CalculateEpoch(item.startDate, item.startTime);

        BString extractedTitle;

        if (fChannelListView != nullptr) {
            int32 selectedIndex = fChannelListView->CurrentSelection();
            if (selectedIndex >= 0) {
                ChannelListItem* listItem = (ChannelListItem*)fChannelListView->ItemAt(selectedIndex);
                if (listItem != nullptr) {
                    BString listRowText(listItem->textDisplay.c_str()); 
                    
                    int32 nowStart = listRowText.FindFirst("(Now: ");
                    if (nowStart != B_ERROR) {
                        int32 titleStart = nowStart + 6;
                        int32 nowEnd = listRowText.FindFirst(")", titleStart);
                        
                        if (nowEnd != B_ERROR) {
                            listRowText.CopyInto(extractedTitle, titleStart, nowEnd - titleStart);
                        }
                    }
                }
            }
        }

        if (extractedTitle.IsEmpty() && fStatusLabel != nullptr) {
            BString fullStatus = fStatusLabel->Text();
            
            if (!fullStatus.StartsWith("Status:")) {
                int32 prefixIndex = fullStatus.FindFirst(" - ");
                if (prefixIndex != B_ERROR) {
                    fullStatus.CopyInto(extractedTitle, prefixIndex + 3, fullStatus.Length());
                } else {
                    extractedTitle = fullStatus;
                    extractedTitle.ReplaceFirst("Selected Lineup: ", "");
                }

                int32 pipeIndex = extractedTitle.FindFirst("|");
                if (pipeIndex != B_ERROR) {
                    extractedTitle.Truncate(pipeIndex);
                }

                int32 closeBracketIndex = extractedTitle.FindFirst("]");
                if (closeBracketIndex != B_ERROR) {
                    BString temp;
                    extractedTitle.CopyInto(temp, closeBracketIndex + 1, extractedTitle.Length());
                    extractedTitle = temp;
                }
            }
        }
        
        extractedTitle.Trim();

        // PERFECT COUPLING CLEANUP: STRIP ALL INTERMEDIATE NOISE TAGS
        extractedTitle.ReplaceAll("Manual_Recording_", "");
        extractedTitle.ReplaceAll("Manual_Recording", "");
        extractedTitle.ReplaceAll("Live_Stream_Available", "");
        extractedTitle.ReplaceAll("Live_Stream", "");
        extractedTitle.Trim();

        BString cleanTitle = extractedTitle;
        cleanTitle.ReplaceAll("/", "-");
        cleanTitle.ReplaceAll(":", "-");
        cleanTitle.ReplaceAll("\\", "-");
        cleanTitle.ReplaceAll("*", "");
        cleanTitle.ReplaceAll("?", "");
        cleanTitle.ReplaceAll(" ", "_"); 
        
        // Squeeze out consecutive duplicate underscores
        while (cleanTitle.FindFirst("__") != B_ERROR) {
            cleanTitle.ReplaceAll("__", "_");
        }
        
        // Trim leading/trailing underscores that remain after stripping text words
        cleanTitle.Trim();
        if (cleanTitle.StartsWith("_")) cleanTitle.Remove(0, 1);
        if (cleanTitle.EndsWith("_")) cleanTitle.Truncate(cleanTitle.Length() - 1);

        // Assign clean parsed title text directly, avoiding "Manual_Recording" completely
        if (cleanTitle.IsEmpty()) {
            config->showTitle = "Live_Stream"; 
        } else {
            config->showTitle = cleanTitle.String(); 
        }

        std::vector<std::string> foundTuners = DiscoverAllTuners();
        BMenuItem* markedTuner = fTunerMenu->FindMarked();

        
        if (markedTuner != nullptr) {
            config->ip = markedTuner->Label();
            tunerAcquired = true;
         } else if (!foundTuners.empty()) {
             config->ip = foundTuners[0]; // Access first indexed entry directly
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
             
             char dateBuffer[32];
             std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", timeInfo);
             
             char timeBuffer[32];
             std::strftime(timeBuffer, sizeof(timeBuffer), "%H-%M", timeInfo);

             config->path = baseDir + "DVR_Record_Ch_" + config->channel + "_" 
                          + dateBuffer + "_" + timeBuffer + "_"
                          + config->showTitle + "_" 
                          + targetDuration + "s_Padded.ts";

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


        case MSG_GUIDE_TOGGLE_FULLSCREEN: {
            if (fGuideWindow != nullptr) {
                if (fGuideWindow->Lock()) {
                    cfg.fullscreenEnable = !cfg.fullscreenEnable;
                    
                    // Using a static scope block tracker preserves coordinates without header alterations
                    static BRect dynamicSavedFrame(100, 100, 1150, 750);
                    
                    if (cfg.fullscreenEnable) {
                        dynamicSavedFrame = fGuideWindow->Frame();
                        
                        BScreen screen(fGuideWindow);
                        if (screen.IsValid()) {
                            fGuideWindow->SetLook(B_NO_BORDER_WINDOW_LOOK);
                            fGuideWindow->SetFlags(fGuideWindow->Flags() | B_NOT_MOVABLE | B_NOT_RESIZABLE);
                            
                            fGuideWindow->MoveTo(screen.Frame().left, screen.Frame().top);
                            fGuideWindow->ResizeTo(screen.Frame().Width(), screen.Frame().Height());
                        }
                    } else {
                        fGuideWindow->SetLook(B_DOCUMENT_WINDOW_LOOK);
                        fGuideWindow->SetFlags(fGuideWindow->Flags() & ~(B_NOT_MOVABLE | B_NOT_RESIZABLE));
                        
                        // Restore original coordinates perfectly
                        fGuideWindow->MoveTo(dynamicSavedFrame.left, dynamicSavedFrame.top);
                        fGuideWindow->ResizeTo(dynamicSavedFrame.Width(), dynamicSavedFrame.Height());
                    }
                    
                    fGuideWindow->Unlock();
                    SaveSchedulesToDisk();
                }
            }
            break;
        }



        case MSG_CHECK_FIRMWARE: {
            fStatusLabel->SetText("Status: Querying tuner hardware profile...");
            
            FirmwareParam* threadArgs = new FirmwareParam();
            threadArgs->targetWindow = this;
            threadArgs->tunerIp = fSelectedIp.c_str(); 
            
            thread_id checkerThread = spawn_thread(FirmwareCheckerWorker, "TunerFirmwareTask", B_NORMAL_PRIORITY, threadArgs);
            if (checkerThread >= 0) {
                resume_thread(checkerThread);
            } else {
                fStatusLabel->SetText("Status: Error spawning firmware worker thread.");
                delete threadArgs; 
            }
            break;
        }


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
   
                        BString localUpgradeUrl;
                        
                        BString currentIp(fSelectedIp.c_str());
                        if (currentIp.IsEmpty()) {
                            currentIp = "127.0.0.1";
                        }
                        
                        localUpgradeUrl.SetToFormat("http://%s/", currentIp.String());
                        
                        if (cfg.debugEnable) {
                            printf("[DEBUG FIRMWARE] Directing native browser onto local device: %s\n", 
                                   localUpgradeUrl.String());
                        }

                        BMessage browserMsg(B_ARGV_RECEIVED);
                        browserMsg.AddString("argv", localUpgradeUrl.String());
                        
                        be_roster->Launch("text/html", &browserMsg);
                    }
                    fStatusLabel->SetText("Status: Opening local tuner upgrade dashboard...");

                }
            }
            break;
        }

        case MSG_PERIODIC_GUIDE_REFRESH: {
            time_t currentTime = real_time_clock();
            BString activeDateWidgetText = fDateInput->Text();
            std::time_t rawToday = std::time(nullptr);
            std::tm* localToday = std::localtime(&rawToday);
            char todayBuf[32];
            std::strftime(todayBuf, sizeof(todayBuf), "%Y-%m-%d", localToday);
            
            bool isViewingToday = (activeDateWidgetText.IsEmpty() || activeDateWidgetText == todayBuf);

            // 1. Keep your network guard intact right here during the periodic loop
            if (fLastNetworkSyncTime == 0 || (currentTime - fLastNetworkSyncTime) >= 86400) {
                if (isViewingToday) {
                    FetchAndPopulateChannelList(activeDateWidgetText); 
                }
                fLastNetworkSyncTime = currentTime;
                if (cfg.debugEnable) printf("[EPG SYNC] Master EPG reloaded from network.\n");
            } 

            // 2. Safely trigger the 5-minute UI increment 
            BMessage fakeTick(MSG_CLOCK_TICK_5MIN);
            this->PostMessage(&fakeTick);
            break;
        }

        case MSG_CLOCK_TICK_5MIN:
        case MSG_CLOCK_UP:
        case MSG_CLOCK_DOWN: {
            std::string timeStr = fTimeInput->Text();
            size_t colonPos = timeStr.find(':');
            if (colonPos != std::string::npos) {
                int hours = std::atoi(timeStr.substr(0, colonPos).c_str());
                int minutes = std::atoi(timeStr.substr(colonPos + 1).c_str());
                
                if (message->what == MSG_CLOCK_TICK_5MIN) {
                    minutes += 5;   
                } else if (message->what == MSG_CLOCK_UP) {
                    minutes += 30;  
                } else {
                    minutes -= 30;  
                }
                
                if (minutes >= 60) { hours += minutes / 60; minutes = minutes % 60; }
                if (minutes < 0) { minutes = 60 + minutes; hours--; } 
                if (hours >= 24) { hours = hours % 24; }
                if (hours < 0) { hours = 23; }
                
                char updatedTimeBuffer[16];
                sprintf(updatedTimeBuffer, "%02d:%02d", hours, minutes);
                fTimeInput->SetText(updatedTimeBuffer);
        
                // 3. Since MSG_PERIODIC_GUIDE_REFRESH already handled the network above,
                // we can pass the string to safely update local arrays or handle manual buttons
                if (fDateInput != nullptr) {
                    FetchAndPopulateChannelList(fDateInput->Text());
                }
        
                BListView* realGuideList = dynamic_cast<BListView*>(FindView("guide_list_view"));
                if (realGuideList != nullptr) {
                    realGuideList->MakeEmpty();
                    realGuideList->Invalidate();
                }
        
                if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
                    BMessage refreshGuide(MSG_PERIODIC_GUIDE_REFRESH);
                    fGuideWindow->PostMessage(&refreshGuide);
                    
                    BView* childHeader = fGuideWindow->FindView("timelineHeader");
                    if (childHeader != nullptr) {
                        BMessage syncHeaderMsg('UCLT');
                        syncHeaderMsg.AddString("time", updatedTimeBuffer);
                        if (fDateInput != nullptr && fDateInput->Text() != nullptr) {
                            syncHeaderMsg.AddString("date", fDateInput->Text());
                        }
                        BMessenger(childHeader).SendMessage(&syncHeaderMsg);
                    }
                    fGuideWindow->Unlock();
                }
            }
            break;
        }


		case MSG_VIEW_RECORDINGS: {
		    if (fRecordingsBrowser == nullptr) {
		        BRect browserFrame(150, 150, 800, 600);
		        fRecordingsBrowser = new RecordingsBrowserWindow(browserFrame, gGlobalSaveDirectory.c_str(), this);
		        fRecordingsBrowser->Show();
		    } else {
		        if (fRecordingsBrowser->Lock()) {
		            fRecordingsBrowser->Activate(true);
		            fRecordingsBrowser->Unlock();
		        }
		    }
		    break;
		}
		
		
		case MSG_RECORDINGS_CLOSED: {
		    fRecordingsBrowser = nullptr; 
		    break;
		}


        case MSG_SHOW_MAIN_SCHEDULER: {
            if (this->IsHidden()) {
                this->Show();
            }
            
            this->Activate(true);
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
	  
	      case MSG_CLOSE_GUIDE_WINDOW: {
	      
	            if (fGuideWindow != nullptr) {
	                if (fGuideWindow->Lock()) {
	                    fGuideWindow->Minimize(true);
	                    fGuideWindow->Unlock();
	                }
	            }
	 
	            break;
	        }        
	        
	         
	      	case MSG_GUIDE_CLOSED: {
	            fGuideWindow = nullptr;
	            
	
	            if (this->IsHidden()) {
	                this->Show();
	                this->Activate(true); 
	                if (cfg.debugEnable) printf("[FRONTEND] Guide closed while Scheduler was hidden. Restoring Scheduler window view.\n");
	            }
	            break;
	        }
	
		 
		   	case MSG_QUIT_ENTIRE_APP: {
		           atomic_set(&gStopScheduler, 1);
		           atomic_set(&gCancelRecording, 1);
		
		           if (fRecordingsBrowser != nullptr) {
		               if (fRecordingsBrowser->Lock()) {
		                   fRecordingsBrowser->Quit();
		                   fRecordingsBrowser = nullptr;
		                }
		           }
		
		           if (fGuideWindow != nullptr) {
		               if (fGuideWindow->Lock()) {
		                   fGuideWindow->Quit();
		                   fGuideWindow = nullptr; 
		               }
		           }
		
		           be_app->PostMessage(B_QUIT_REQUESTED);
		
		           this->Quit(); 
		           break;
		       }
		
		      case MSG_ABORT_SPECIFIC_RECORDING: {
		
		            const char* targetPath = nullptr;
		            if (message->FindString("file_path", &targetPath) == B_OK) {
		                BMessage serviceForwarder(MSG_ABORT_SPECIFIC_RECORDING);
		                serviceForwarder.AddString("file_path", targetPath);
		                
		                BMessenger serviceApp("application/x-vnd.haikuhdhomerun-dvr");
		                if (serviceApp.IsValid()) {
		                    serviceApp.SendMessage(&serviceForwarder);
		                  if (cfg.debugEnable) printf("[FRONTEND] Forwarded active recording abort request to backend service.\n");
		                } else {
		                  if (cfg.debugEnable) printf("[FRONTEND ERROR] Backend Service daemon signature not found!\n");
		                }
		            }
		            break;
		        }
		
		       case MSG_SET_PLAYER_MPV: {
		            cfg.defaultPlayer = "MPV";
		            fPlayerMpvItem->SetMarked(true);
		            fPlayerMediaItem->SetMarked(false);
		            fPlayerVlcItem->SetMarked(false);
		            fPlayerHtvItem->SetMarked(false); 
		            SaveSchedulesToDisk();
		            break;
		        }
		        
		       case MSG_SET_PLAYER_MEDIAPLAYER: {
		            cfg.defaultPlayer = "MediaPlayer";
		            fPlayerMpvItem->SetMarked(false);
		            fPlayerMediaItem->SetMarked(true);
		            fPlayerVlcItem->SetMarked(false);
		            fPlayerHtvItem->SetMarked(false); 
		            SaveSchedulesToDisk();
		            break;
		        }
		        
		       case MSG_SET_PLAYER_VLC: {
		            cfg.defaultPlayer = "VLC";
		            fPlayerMpvItem->SetMarked(false);
		            fPlayerMediaItem->SetMarked(false);
		            fPlayerVlcItem->SetMarked(true);
		            fPlayerHtvItem->SetMarked(false);
		            SaveSchedulesToDisk();
		            break;
		        }

		       case MSG_SET_PLAYER_HTV: {
		            cfg.defaultPlayer = "hTV";
		            fPlayerMpvItem->SetMarked(false);
		            fPlayerMediaItem->SetMarked(false);
		            fPlayerVlcItem->SetMarked(false);
		            fPlayerHtvItem->SetMarked(true); 
		            SaveSchedulesToDisk();
		            break;
		        }

		         
	     default:
	         BWindow::MessageReceived(message);
	         break;
	        }
      }
};


// =========================================================================
// RealTVGuideWindow Method Definitions 
// =========================================================================
void RealTVGuideWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
    	
       case B_KEY_DOWN: {
                const char* bytes = nullptr;
                if (message->FindString("bytes", &bytes) == B_OK && bytes[0] == B_ESCAPE) {
                    PostMessage(B_QUIT_REQUESTED);
                }
                break;
            }
    
            case MSG_DATE_SELECTED: {
            DVRWindow* mainWindow = dynamic_cast<DVRWindow*>(fMainAppWindow);
            
            if (mainWindow != nullptr) {
                if (fContainerList != nullptr) {
                    int32 itemCount = fContainerList->CountItems();
                    for (int32 i = itemCount - 1; i >= 0; i--) {
                        BListItem* item = fContainerList->RemoveItem(i);
                        delete item;
                    }
                }

                const std::vector<ChannelGuideItem>& freshChannels = mainWindow->GetLoadedChannels();

                _BuildGuideRowsFromLiveChannels(freshChannels, fMainChannelListView);

                if (fContainerList != nullptr) {
                    fContainerList->ScrollTo(0.0f, 0.0f); 
                    fContainerList->Invalidate();
                }
            }
            break;
        }

            case MSG_SHOW_DATE_PICKER: {
            if (fMainAppWindow != nullptr) {
                BMessage openMainCalendar(MSG_OPEN_CALENDAR_PANEL); 
                fMainAppWindow->PostMessage(&openMainCalendar);
            }
            break;
        }

            
        case MSG_REFRESH_CHANNEL_LIST_ICONS: {
            if (fContainerList != nullptr) {
                int32 guideItemCount = fContainerList->CountItems();
                
                for (int32 idx = 0; idx < guideItemCount; idx++) {
                    GuideListRowItem* guideRow = static_cast<GuideListRowItem*>(fContainerList->ItemAt(idx));
                    
                    if (guideRow != nullptr && guideRow->fData.channelIcon == nullptr) {
                        BString cleanName = guideRow->fData.channelLabel;
                        int32 dashIndex = cleanName.FindFirst("-");
                        if (dashIndex != B_ERROR) {
                            BString temp;
                            cleanName.CopyInto(temp, dashIndex + 1, cleanName.Length());
                            cleanName = temp;
                        }
                        cleanName.Trim();

                        std::string iconPath = "/boot/home/config/settings/HaikuDVR/icons/" + std::string(cleanName.String()) + ".png";
                        
                        std::ifstream checkFile(iconPath.c_str());
                        bool existsOnDisk = checkFile.good();
                        checkFile.close();

                        if (existsOnDisk) {
                            BBitmap* freshIcon = BTranslationUtils::GetBitmap(iconPath.c_str());
                            if (freshIcon != nullptr && freshIcon->IsValid()) {
                                guideRow->fData.channelIcon = freshIcon;
                                fContainerList->InvalidateItem(idx); 
                            } else if (freshIcon) {
                                delete freshIcon;
                            }
                        }
                    }
                }
            }
            break;
        }


    	
			case MSG_PERIODIC_GUIDE_REFRESH: {
			    DVRWindow* mainWindow = dynamic_cast<DVRWindow*>(fMainAppWindow);
			    
			    if (mainWindow != nullptr) {
			        float currentXScroll = 0.0f;
			        float currentYScroll = 0.0f;
			        
			        if (fContainerList != nullptr) {
			            currentXScroll = fContainerList->Bounds().left;
			            currentYScroll = fContainerList->Bounds().top;
			        }
			
			        // Clear out old items safely
			        int32 itemCount = fContainerList->CountItems();
			        for (int32 i = itemCount - 1; i >= 0; i--) {
			            BListItem* item = fContainerList->RemoveItem(i);
			            delete item;
			        }
			
			        // Explicitly lock the Main Window before touching its vector!
			        std::vector<ChannelGuideItem> localCopy;
			        if (mainWindow->Lock()) {
			            localCopy = mainWindow->GetLoadedChannels(); // Makes a thread-safe snapshot copy
			            mainWindow->Unlock();
			        }
			
			        // Build using the safe local copy, not the live moving cross-thread reference
			        _BuildGuideRowsFromLiveChannels(localCopy, fMainChannelListView);
			
			        if (fContainerList != nullptr) {
			            fContainerList->ScrollTo(currentXScroll, currentYScroll);
			        }
			
			        fContainerList->Invalidate();
			    }
			    break;
			}

        default:
            BWindow::MessageReceived(message);
            break;
    }
}




DVRWindow::~DVRWindow() {
    if (fGuideWindow != nullptr) {
        if (fGuideWindow->Lock()) {
            fGuideWindow->Quit();
        }
    }

    if (fRecordingsBrowser != nullptr) {
        if (fRecordingsBrowser->Lock()) {
            fRecordingsBrowser->Quit();
        }
    }

    delete fFolderPanel;
    delete fRefreshRunner;
    
    for (BBitmap* bitmap : fIconCache) {
        delete bitmap;
    }
    fIconCache.clear();
}

bool DVRWindow::QuitRequested() {
    BWindow* activeWin = nullptr;
    int32 windowIndex = 0;
    bool guideIsOpenOnScreen = false;
    
    while ((activeWin = be_app->WindowAt(windowIndex)) != nullptr) {
        if (strcmp(activeWin->Title(), "Interactive TV Guide Matrix") == 0) {
            guideIsOpenOnScreen = true;
            break; 
        }
        windowIndex++;
    }

    if (guideIsOpenOnScreen) {
       // this->Hide(); 
        this->Minimize(true);
        return false; 
    }

    atomic_set(&gStopScheduler, 1);
    atomic_set(&gCancelRecording, 1);

    if (fRecordingsBrowser != nullptr) {
        if (fRecordingsBrowser->Lock()) {
            fRecordingsBrowser->Quit();
            fRecordingsBrowser = nullptr;
        }
    }

    be_app->PostMessage(B_QUIT_REQUESTED);
    return true; 
}


class DVRApplication : public BApplication {
public:
    DVRApplication() : BApplication("application/x-vnd.haikuhdhomerun-dvr-gui") {}
    
    void ReadyToRun() override {
        DVRWindow* window = new DVRWindow();
        window->Show();        
       if (cfg.fullscreenEnable) window->PostMessage(MSG_OPEN_GUIDE);
    }

};


int main() {
	ensure_config_dir();
    DVRApplication app;
    app.Run();
    return 0;
}

             
