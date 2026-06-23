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
#include <app/MessageRunner.h>
#include <StorageKit.h>
#include <StringList.h>
#include <Screen.h>

namespace AppInfo {
    static const char* const VERSION_STRING = "HaikuDVR v1.0.14 (Haiku OS)";
}



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
    bool debugEnable = true; 
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
    std::string channel;
    std::string duration; 
    std::string showTitle;
    std::string channelLabel;  
    bool processed;
    std::string tunerIp; 
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
    
    jRoot["save_directory"] = gGlobalSaveDirectory; 

    jRoot["show_update_notifications"] = cfg.showUpdateNotifications; 
    jRoot["debug_enable"]              = cfg.debugEnable;
    jRoot["default_player"]            = cfg.defaultPlayer;
    
    json jSchedules = json::array();
    for (const auto& item : gScheduleList) {
        if (!item.processed) {
            jSchedules.push_back({
                {"date", item.startDate}, 
                {"time", item.startTime},
                {"channel", item.channel},
                {"duration", item.duration},
                {"tuner_ip", item.tunerIp},
                {"show_title", item.showTitle}
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
            gGlobalSaveDirectory        = jIn.value("save_directory", "/boot/home");
            cfg.showUpdateNotifications = jIn.value("show_update_notifications", true);
            cfg.debugEnable             = jIn.value("debug_enable", true);
            cfg.defaultPlayer           = jIn.value("default_player", "hTV"); // Handled dynamically!
            
            if (jIn.contains("schedules") && jIn["schedules"].is_array()) {
                gScheduleList.clear();
                for (const auto& entry : jIn["schedules"]) {
                    ScheduleItem item;
                    item.startDate = entry.value("date", "2026-06-23"); // Updated fallback to current date
                    item.startTime = entry.value("time", "12:00");
                    item.channel   = entry.value("channel", "5.1");
                    item.duration  = entry.value("duration", "1800");
                    item.tunerIp   = entry.value("tuner_ip", ""); 
                    item.showTitle = entry.value("show_title", "Unknown_Show");
                    item.processed = false;
                    gScheduleList.push_back(item);
                }
            }
        }
        else if (jIn.is_array()) {
            cfg.showUpdateNotifications = true;
            cfg.debugEnable             = true;
            cfg.defaultPlayer           = "MPV"; 
            
            gScheduleList.clear();
            for (const auto& entry : jIn) {
                ScheduleItem item;
                item.startDate = entry.value("date", "2026-06-23"); // Updated fallback to current date
                item.startTime = entry.value("time", "12:00");
                item.channel   = entry.value("channel", "5.1");
                item.duration  = entry.value("duration", "1800");
                item.tunerIp   = entry.value("tuner_ip", "");
                item.showTitle = entry.value("show_title", "Unknown_Show"); 
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





static int32 BackgroundUpdateChecker(void* data) {
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
	BStringView* fTotalUsageLabel;
    BListView* fRecordingsList;
    BWindow*   fMainAppWindow;
    BString    fRecordingDir;
	BButton*   fRefreshButton;
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

            BString expectedLabel;
            expectedLabel << name << " (" << _FormatFileSize(fileSize) << ")";

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
            footerLabel << "Total Library Footprint Storage: " << _FormatFileSize(totalAccumulatedBytes);
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
	        fCalendar->SetInvocationMessage(new BMessage(MSG_DATE_SELECTED));	        
	        fCalendar->SetFlags(fCalendar->Flags() | B_NAVIGABLE | B_WILL_DRAW);	        
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

            this->Lock();
            this->Quit();
        } else {
            BWindow::MessageReceived(message);
        }
    }
    
    void DispatchMessage(BMessage* message, BHandler* handler) override {
        if (message->what == B_MOUSE_UP && fCalendar != nullptr) {
            BPoint mousePos;
            uint32 buttons;
            fCalendar->GetMouse(&mousePos, &buttons);
            
            if (fCalendar->Bounds().Contains(mousePos)) {
                snooze(10000); 
                PostMessage(MSG_DATE_SELECTED);
                return;
            }
        }
        BWindow::DispatchMessage(message, handler);
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




// =========================================================================
//  HEADER VIEW WITH PERFECTLY CENTERED GLYPHS AND MELLOW HOVER GLOWS
// =========================================================================
class TimelineHeaderView : public BView {
private:
    BRect fDateClickRect;
    BRect fTimeDownRect;
    BRect fTimeUpRect;
    BWindow* fMainAppTarget;
    BString fCachedSelectedTime;
    BString fCachedSelectedDate;

    bool fHoveringMinus;
    bool fHoveringPlus;

    // Helper method to draw a glyph perfectly centered inside a rectangle
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
    TimelineHeaderView(BRect frame, BWindow* mainAppTarget) 
        : BView(frame, "timelineHeader", B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW | B_NAVIGABLE) {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        
        fMainAppTarget = mainAppTarget;
        fHoveringMinus = false;
        fHoveringPlus = false;
        
        fCachedSelectedTime = "4:00 PM";
        fCachedSelectedDate = "2026-06-22";
        
        fDateClickRect.Set(90.0, 0.0, 185.0, frame.Height());
        fTimeDownRect.Set(200.0, 8.0, 222.0, 32.0);
        fTimeUpRect.Set(230.0, 8.0, 252.0, 32.0);

        // CRITICAL FOR HOVER: Tell Haiku to send mouse tracking events even if buttons aren't clicked
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
                int32 selHour = 12, selMin = 0;
                
               
                if (std::sscanf(processedSelectedTime.String(), "%d:%d", &selHour, &selMin) == 2) {
                    char ampmBuf[16];
                    std::snprintf(ampmBuf, sizeof(ampmBuf), "%s", (selHour >= 12 ? "PM" : "AM"));
                    
                    int32 displayHour = (selHour > 12) ? (selHour - 12) : ((selHour == 0) ? 12 : selHour);
                    processedSelectedTime.SetToFormat("%d:%02d %s", displayHour, selMin, ampmBuf);
                }

                // =========================================================================
                // 2. TYPESET UNIFIED DATA STRINGS WITH ALIGNED TABLE COLUMNS
                // =========================================================================
                BString timePart1, timePart2;
                timePart1.SetToFormat("Current Time: %s", cleanLiveTime.String());
                timePart2.SetToFormat("Selected Time: %s", processedSelectedTime.String());
                
                BString datePart1, datePart2;
                datePart1.SetToFormat("Current Date: %s", liveDateBuf);
                datePart2.SetToFormat("Selected Date: %s", fCachedSelectedDate.String());

                float firstColumnWidth = 125.0f;
                float separatorOffset = currentLeft + 16.0f + firstColumnWidth;

                // =========================================================================
                // 3. PAINT GRID STRINGS (ALL UNIFIED IN CRISP TEXTURE WHITE)
                // =========================================================================
                SetHighColor(textColor); 
                
                MovePenTo(currentLeft + 16.0f, bounds.top + 16.0f);
                DrawString(timePart1.String());
                
                MovePenTo(separatorOffset, bounds.top + 16.0f);
                DrawString("|");
                
                MovePenTo(separatorOffset + 14.0f, bounds.top + 16.0f);
                DrawString(timePart2.String());

                MovePenTo(currentLeft + 16.0f, bounds.top + 31.0f);
                DrawString(datePart1.String());
                
                MovePenTo(separatorOffset, bounds.top + 31.0f);
                DrawString("|");
                
                MovePenTo(separatorOffset + 14.0f, bounds.top + 31.0f);
                DrawString(datePart2.String());

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
        if (fMainAppTarget != nullptr) {
            BMessenger targetMessenger(fMainAppTarget);

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
        }
        BView::MouseDown(point);
    }
    
    void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case 'UCLT': { 
                const char* newTime = nullptr;
                const char* newDate = nullptr;
                
                if (message->FindString("time", &newTime) == B_OK) fCachedSelectedTime = newTime;
                if (message->FindString("date", &newDate) == B_OK) fCachedSelectedDate = newDate;
                
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
        textDisplay = text;
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
//  LIST ITEM WITH HOVER TRACKING AND CELL CLICK INTERFACE
// =========================================================================
class GuideListRowItem : public BListItem {
public:
    GuideRowModel fData;
    int32 fRowIndex;
    int32 fHoveredCellIndex; 

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
                
                if (isContinuation) {
                    continue;
                }

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
                
                selectionBroadcast.AddString("start_time", selectedProg.timeDisplay.String());
                selectionBroadcast.AddString("channel_label", fData.channelLabel.String());
                selectionBroadcast.AddInt32("duration_minutes", selectedProg.durationMinutes);
                
                BString targetSubchannel = fData.channelLabel;
                int32 spaceIndex = targetSubchannel.FindFirst(" ");
                if (spaceIndex != B_ERROR) {
                    targetSubchannel.Truncate(spaceIndex);
                }
                targetSubchannel.Trim();
                selectionBroadcast.AddString("numeric_subchannel", targetSubchannel.String());

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

            // F. Compare entries with the active recording scheduling queue
            bool isScheduled = false;
            for (size_t i = 0; i < gScheduleList.size(); i++) {
                if (!gScheduleList[i].processed) {
                    bool channelMatch = (gScheduleList[i].channel.find(targetChannel) != std::string::npos);
                    bool timeMatch = (gScheduleList[i].startTime == normalizedCellTime);
                    bool dateMatch = currentViewDateStr.empty() ? true : (gScheduleList[i].startDate == currentViewDateStr);

                    if (channelMatch && timeMatch && dateMatch) {
                        isScheduled = true;
                        break;
                    }
                }
            }

            // =========================================================================
            // G. DRAW MATRIX CARD BACKGROUND SHAPES & INTERACTIVE HOVER GLOWS
            // =========================================================================
            rgb_color normalCellBg    = { 26, 26, 26, 255 };      // Deep charcoal card backing
            rgb_color standardBorder  = { 45, 45, 45, 255 };      // Subtle clean card separator grid
            rgb_color scheduledBgColor = { 75, 20, 20, 255 };     // Rich crimson backing for recording element
            rgb_color borderRed        = { 220, 40, 40, 255 };    // High-visibility scarlet red stroke
            
            rgb_color glowBorderColor = { 0, 210, 210, 255 };     // Vivid neon teal tracking boundary
            rgb_color glowInnerColor  = { 12, 45, 45, 255 };      // Soft glowing teal canvas blend

            if (isScheduled) {
                owner->PushState(); 
                owner->SetHighColor(scheduledBgColor);
                owner->FillRect(cellRect);
                owner->SetLowColor(scheduledBgColor);
            } else {
                if ((int32)idx == fHoveredCellIndex) {
                    owner->SetHighColor(glowInnerColor);
                } else {
                    owner->SetHighColor(normalCellBg);
                }
                owner->FillRect(cellRect);
                owner->SetLowColor((int32)idx == fHoveredCellIndex ? glowInnerColor : normalCellBg);
            }

            // Draw Outline Framing Graphics
            if (isScheduled) {
                if ((int32)idx == fHoveredCellIndex) {
                    owner->SetHighColor(glowBorderColor); 
                } else {
                    owner->SetHighColor(borderRed);
                }
                owner->StrokeRect(cellRect);
                owner->StrokeRect(cellRect.InsetByCopy(1.0, 1.0)); 
            } else {
                if ((int32)idx == fHoveredCellIndex) {
                    owner->SetHighColor(glowBorderColor);
                    owner->StrokeRect(cellRect);
                    owner->StrokeRect(cellRect.InsetByCopy(1.0, 1.0)); 
                } else {
                    owner->SetHighColor(standardBorder);
                    owner->StrokeRect(cellRect);
                }
            }

            // =========================================================================
            // H. FIELD STRINGS PLACEMENTS (WITH TIME RANGES & SYNOPSIS)
            // =========================================================================
            BFont timeFont;
            owner->GetFont(&timeFont);
            timeFont.SetFace(B_REGULAR_FACE);
            timeFont.SetSize(10.0); 
            owner->SetFont(&timeFont);

            if (isScheduled) {
                owner->SetHighColor(255, 140, 140, 255);
            } else {
                owner->SetHighColor(140, 140, 140, 255); 
            }

            // Read the end time directly from the local block structure
            BString endString = prog.endTimeStr;
            if (endString.IsEmpty()) {
                time_t estEndEpoch = rawToday + (idx * 30 * 60) + (30 * 60);
                struct tm* estTm = std::localtime(&estEndEpoch);
                
                char estBuf[32] = {0}; 
                std::strftime(estBuf, sizeof(estBuf), "%I:%M %p", estTm);
                endString = estBuf;
                if (endString.StartsWith("0")) { endString.Remove(0, 1); }
            }

            BString timeRangeStr;
            timeRangeStr.SetToFormat("%s - %s", displayTimeText.String(), endString.String());

            owner->MovePenTo(cellRect.left + 20, cellRect.top + 28);
            owner->DrawString(timeRangeStr.String()); 

            // B. Draw Bold Program Title
            BFont titleFont;
            owner->GetFont(&titleFont);
            titleFont.SetFace(B_BOLD_FACE);
            titleFont.SetSize(12.0); 
            owner->SetFont(&titleFont);

            if (isScheduled) {
                owner->SetHighColor(255, 255, 255, 255); 
            } else {
                owner->SetHighColor(textColor);         
            }

            owner->MovePenTo(cellRect.left + 20, cellRect.top + 50);
            BString truncatedTitle = prog.title;
            owner->TruncateString(&truncatedTitle, B_TRUNCATE_END, cellRect.Width() - 32);
            owner->DrawString(truncatedTitle.String());

            // C. Draw Description Synopsis Text Paragraph
            BFont descFont;
            owner->GetFont(&descFont);
            descFont.SetFace(B_ITALIC_FACE);
            descFont.SetSize(11.0); 
            owner->SetFont(&descFont);

            if (isScheduled) {
                owner->SetHighColor(220, 200, 200, 255); 
            } else {
                owner->SetHighColor(170, 170, 170, 255); 
            }

            owner->MovePenTo(cellRect.left + 20, cellRect.top + 72);
            
            BString shortDesc = prog.description;
            if (shortDesc.IsEmpty()) { 
                shortDesc = "No further program description text details provided by broadcaster."; 
            }
            
            owner->TruncateString(&shortDesc, B_TRUNCATE_END, cellRect.Width() - 32);
            owner->DrawString(shortDesc.String());

            // =========================================================================
            // I. EXTRA LAYER FLAGS (RECORD BADGES ACCENT)
            // =========================================================================
            if (isScheduled) {
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
                        std::string targetTime = item->fData.programs[cellIndex].timeDisplay.String();
                        std::string targetChannel = cleanNumberOnly.String(); // e.g., "2.1"
                        std::string targetTitle = item->fData.programs[cellIndex].title.String();

                        // =========================================================================
                        // EXTRACT CALENDAR DATE CONTEXT UPFRONT
                        // =========================================================================
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
                            
                            if (dateInput != nullptr && dateInput->Text() != nullptr) {
                                currentViewDateStr = dateInput->Text();
                            }
                            if (timeInput != nullptr && timeInput->Text() != nullptr) {
                                currentViewTimeStr = timeInput->Text();
                            }
                        }

                        bool isViewingFutureDay = (currentViewDateStr != todayStr);

                        // =========================================================================
                        // 24-HOUR NORMALIZATION (MATCHES DRAW LOGIC & AUTO-COMMIT SCHEDULER)
                        // =========================================================================
                        std::string normalizedCellTime = "";

                        if (targetTime == "LIVE NOW") {
                            if (isViewingFutureDay) {
                                int32 h = 12, m = 0;
                                if (sscanf(currentViewTimeStr.c_str(), "%d:%d", &h, &m) >= 1) {
                                    m += (int32)(cellIndex * 30);
                                    h += m / 60;
                                    m = m % 60;
                                    h = h % 24;
                                    
                                    char rawBuffer[32];
                                    snprintf(rawBuffer, sizeof(rawBuffer), "%02d:%02d", h, m);
                                    normalizedCellTime = rawBuffer;
                                }
                            } else {
                                char rawBuffer[32];
                                snprintf(rawBuffer, sizeof(rawBuffer), "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
                                normalizedCellTime = rawBuffer;
                            }
                        } 
                        else if (targetTime == "NEXT") {
                            time_t nextBlock = now + (30 * 60);
                            struct tm* nextInfo = localtime(&nextBlock);
                            char rawBuffer[32];
                            snprintf(rawBuffer, sizeof(rawBuffer), "%02d:%02d", nextInfo->tm_hour, nextInfo->tm_min);
                            normalizedCellTime = rawBuffer;
                        } 
                        else if (targetTime == "LATER") {
                            time_t laterBlock = now + (60 * 60);
                            struct tm* laterInfo = localtime(&laterBlock);
                            char rawBuffer[32];
                            snprintf(rawBuffer, sizeof(rawBuffer), "%02d:%02d", laterInfo->tm_hour, laterInfo->tm_min);
                            normalizedCellTime = rawBuffer;
                        } 
                        else {
                            int hours = 0, minutes = 0;
                            char ampm[16] = {0};
                            if (sscanf(targetTime.c_str(), "%d:%d %15s", &hours, &minutes, ampm) >= 2) {
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
                                normalizedCellTime = targetTime; 
                            }
                        }

                        gScheduleLocker.Lock();

                        int32 activeCounter = 0;
                        for (size_t i = 0; i < gScheduleList.size(); i++) {
                            if (!gScheduleList[i].processed) {
                                bool channelMatch = (gScheduleList[i].channel.find(targetChannel) != std::string::npos);
                                
                                bool timeMatch = (gScheduleList[i].startTime == normalizedCellTime);

                                if (channelMatch && timeMatch) { 
                                    matchingActiveIndex = activeCounter;
                                    break;
                                }
                                activeCounter++;
                            }
                        }
                        gScheduleLocker.Unlock();

                        if (matchingActiveIndex != -1) {
                            removeItem = new BMenuItem("Remove Queue", NULL);
                            contextMenu->AddItem(removeItem);
                        } else {
                            queueItem = new BMenuItem("Add to Queue", NULL);
                            contextMenu->AddItem(queueItem);
                        }

                        contextMenu->AddSeparatorItem();
                        BMenuItem* viewRecsItem = new BMenuItem("Open Recordings", new BMessage(MSG_VIEW_RECORDINGS));
                        contextMenu->AddItem(viewRecsItem);

                        BMenuItem* showSchedItem = new BMenuItem("Open Scheduler", new BMessage(MSG_SHOW_MAIN_SCHEDULER));
                        contextMenu->AddItem(showSchedItem);

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

                            selectionBroadcast.AddString("start_time", targetTimeStr.String());
                            selectionBroadcast.AddBool("auto_commit_queue", true);
                            
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
    BListView* fContainerList;
    BWindow* fMainAppWindow;
    BListView* fMainChannelListView;


// @Datapusher
void _BuildGuideRowsFromLiveChannels(const std::vector<ChannelGuideItem>& loadedChannels, BListView* mainListView) {
	    if (mainListView == nullptr) return;
	
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
	
	        rowData.programs.push_back({ 
	            liveChan.nowPlaying.c_str(), 
	            finalColumn1Time.String(), 
	            350.0f, 
	            liveChan.nowPlayingDurationMinutes,
	            liveChan.nowPlayingEndTimeStr.c_str(),  
	            liveChan.nowPlayingDescription.c_str()   
	        });
	        
	        for (const auto& nextShow : liveChan.futureLineup) {
	            BString nextTimeLabel = nextShow.startTimeStr.c_str();
	            
	            int hNext = 0, mNext = 0;
	            if (nextTimeLabel.FindFirst("PM") == B_ERROR && nextTimeLabel.FindFirst("AM") == B_ERROR) {
	                if (sscanf(nextTimeLabel.String(), "%d:%d", &hNext, &mNext) == 2) {
	                    nextTimeLabel.SetToFormat("%d:%02d %s", (hNext > 12 ? hNext - 12 : (hNext == 0 ? 12 : hNext)), mNext, (hNext >= 12 ? "PM" : "AM"));
	                }
	            }
	
	            rowData.programs.push_back({
	                nextShow.title.c_str(), 
	                nextTimeLabel.String(), 
	                350.0f, 
	                nextShow.durationMinutes,
	                nextShow.endTimeStr.String(),     
	                nextShow.description.String()      
	            });
	        }
	        
	        fContainerList->AddItem(new GuideListRowItem(rowData, i));
	    }
	}
};




class DVRWindow : public BWindow {

private:
	std::string fCachedGuidePayload; 
	BMenuItem *fPlayerMpvItem, *fPlayerMediaItem, *fPlayerHtvItem, *fPlayerVlcItem;
   	BWindow* fRecordingsBrowser = nullptr;
	BWindow* fGuideWindow = nullptr; 
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
	BMessageRunner* fRefreshRunner;
	time_t   fLastNetworkSyncTime; 
					
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

    uint32 currentTime = real_time_clock();
    uint32 cacheExpirationWindow = 14400; 

    if (savedSyncTime > 0 && (currentTime - savedSyncTime) < cacheExpirationWindow) {
       // uint32 remainingTime = cacheExpirationWindow - (currentTime - savedSyncTime);
       //  std::printf("[DVR DEBUG] SUCCESS: Local cache is only %d seconds old (Expires in %d mins). Skipping network check completely!\n", 
       //             (currentTime - savedSyncTime), remainingTime / 60);
        return false; // Local cache is perfectly valid!
    }

    if (cfg.debugEnable) std::printf("[DVR DEBUG] Cache window expired or invalid. Ready to sync with remote server.\n");
    return true;
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
            if (rawTimeText.IFindFirst("PM") != B_ERROR && targetHour < 12) {
                targetHour += 12;
            } else if (rawTimeText.IFindFirst("AM") != B_ERROR && targetHour == 12) {
                targetHour = 0;
            }
        }
    }

    if (targetMin < 30) {
        targetMin = 0;
    } else {
        targetMin = 30;
    }

    std::tm targetTimeBox = {0};
    targetTimeBox.tm_year = targetYear - 1900;
    targetTimeBox.tm_mon  = targetMonth - 1;
    targetTimeBox.tm_mday = targetDay;
    targetTimeBox.tm_hour = targetHour;
    targetTimeBox.tm_min  = targetMin;
    targetTimeBox.tm_sec  = 0;
    targetTimeBox.tm_isdst = -1; 
    std::time_t targetComparisonEpoch = std::mktime(&targetTimeBox);

    // =========================================================================
    // 🚀 THREAD-SAFE HEADER SYNC PASS (PREVENTS DEADLOCKS SYSTEM-WIDE)
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

    std::map<std::string, ChannelGuideItem> cloudGuideMap;
    std::map<std::string, std::string> xmlIdToChannelNumMap; 
    bool isFilteringFutureDay = (targetDateStr != nullptr && std::strlen(targetDateStr) > 0);

    bool needNetworkFetch = fCachedGuidePayload.empty() || 
                            fLastNetworkSyncTime == 0 || 
                            (real_time_clock() - fLastNetworkSyncTime) >= 86400 ||
                            isFilteringFutureDay;

    std::string xmlCachePath = "/boot/home/config/settings/HaikuDVR/guide.xml";

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
           //  if (cfg.debugEnable) std::printf("[DVR NETWORK] Fetching Master XMLTV Payload: %s\n", xmltvUrl.c_str());  
        
	        if (!IsRemoteFileNewer(xmltvUrl, xmlCachePath)) {
	            fLastNetworkSyncTime = real_time_clock();
	            fCachedGuidePayload = "LOADED";
	            needNetworkFetch = false; 
	        }
           

	        if (needNetworkFetch) {
	            FILE* xmlFile = std::fopen(xmlCachePath.c_str(), "wb");
	            curl = curl_easy_init();
	            if (curl && xmlFile) {
	                curl_easy_setopt(curl, CURLOPT_URL, xmltvUrl.c_str());
	                curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 HaikuDVR/1.0");
	                curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
	                curl_easy_setopt(curl, CURLOPT_WRITEDATA, xmlFile);
	                
	                curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
	                

	                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); 
	                
	                CURLcode res = curl_easy_perform(curl);
	                
	                if (res == CURLE_OK) {
	                    uint32 syncTimestamp = real_time_clock();
	                    fLastNetworkSyncTime = syncTimestamp;
	                    fCachedGuidePayload = "LOADED";
	                    
	                    std::string cacheControlPath = xmlCachePath + ".cache";
	                    std::ofstream cacheOut(cacheControlPath);
	                    if (cacheOut.is_open()) {
	                        cacheOut << syncTimestamp << "\n";
	                        cacheOut.close();
	                        if (cfg.debugEnable) std::printf("[DVR DEBUG] Saved fresh sync timestamp token: %u\n", syncTimestamp);
	                    }
	                }

	                curl_easy_cleanup(curl);
	                std::fclose(xmlFile);
	
	                if (res != CURLE_OK) {
	                    fStatusLabel->SetText("Status: Cloud XMLTV download failed.");
	                    return;
	                }
	            } else if (xmlFile) {
	                std::fclose(xmlFile);
	            }
	        }
        }
    }




    std::ifstream xmlFile(xmlCachePath.c_str());
    if (!xmlFile.is_open()) {
        fStatusLabel->SetText("Status: Error - guide.xml could not be opened from disk.");
        return;
    }

    std::string xmlLine;
    std::string currentChannelId = "";

    // -------------------------------------------------------------------------
    // PASS 1: MAP STATION IDS TO LOGICAL SUBCHANNEL NUMBERS & CLOUD ICONS
    // -------------------------------------------------------------------------
    std::map<std::string, std::string> cloudIconMap; 

    while (std::getline(xmlFile, xmlLine)) {
        size_t chanPos = xmlLine.find("<channel id=\"");
        if (chanPos != std::string::npos) {
            size_t startIdx = chanPos + 13;
            size_t endIdx = xmlLine.find("\"", startIdx);
            if (endIdx != std::string::npos) {
                currentChannelId = xmlLine.substr(startIdx, endIdx - startIdx);
            }
            continue;
        }

        size_t lcnPos = xmlLine.find("<lcn>");
        if (lcnPos != std::string::npos && !currentChannelId.empty()) {
            size_t startIdx = lcnPos + 5;
            size_t endIdx = xmlLine.find("</lcn>", startIdx);
            if (endIdx != std::string::npos) {
                std::string lcnVal = xmlLine.substr(startIdx, endIdx - startIdx);
                xmlIdToChannelNumMap[currentChannelId] = lcnVal;
                
                ChannelGuideItem item;
                item.guideNumber = lcnVal;
                item.nowPlaying  = "To Be Announced";
                item.nowPlayingDurationMinutes = 30;
                cloudGuideMap[lcnVal] = item;
            }
            continue; 
        }

        size_t iconPos = xmlLine.find("<icon src=\"");
        if (iconPos != std::string::npos && !currentChannelId.empty()) {
            size_t startIdx = iconPos + 11;
            size_t endIdx = xmlLine.find("\"", startIdx);
            if (endIdx != std::string::npos) {
                std::string iconUrl = xmlLine.substr(startIdx, endIdx - startIdx);
                std::string chNum = xmlIdToChannelNumMap[currentChannelId];
                if (!chNum.empty()) {
                    cloudIconMap[chNum] = iconUrl; 
                }
            }
            currentChannelId = ""; 
        }

        if (xmlLine.find("<programme") != std::string::npos) {
            break; 
        }
    }


    // -------------------------------------------------------------------------
    // PASS 2: STREAM PARSE
    // -------------------------------------------------------------------------
    xmlFile.clear();
    xmlFile.seekg(0, std::ios::beg);

    std::string progChanId = "";
    std::string progStartRaw = "";
    std::string progEndRaw = "";
    std::string titleText = "";
    std::string descText = ""; 

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
        tmTime.tm_year = y - 1900;
        tmTime.tm_mon  = mo - 1;
        tmTime.tm_mday = d;
        tmTime.tm_hour = h;
        tmTime.tm_min  = m;
        tmTime.tm_sec  = s;
        tmTime.tm_isdst = -1;

        long offsetSeconds = 0;
        size_t spacePos = rawXmlTime.find(' ');
        if (spacePos != std::string::npos && spacePos + 5 <= rawXmlTime.length()) {
            int sign = (rawXmlTime[spacePos + 1] == '-') ? -1 : 1;
            int oh = 0, om = 0;
            std::sscanf(rawXmlTime.substr(spacePos + 2, 2).c_str(), "%2d", &oh);
            std::sscanf(rawXmlTime.substr(spacePos + 4, 2).c_str(), "%2d", &om);
            offsetSeconds = sign * ((oh * 3600) + (om * 60));
        }

        std::time_t localEpoch = std::mktime(&tmTime);
        
        std::time_t sysNow = std::time(nullptr);
        std::tm* sysLocal = std::localtime(&sysNow);
        std::tm tmCopy = *sysLocal;
        std::time_t sysLocalEpoch = std::mktime(&tmCopy);
        std::tm* sysUtc = std::gmtime(&sysNow);
        tmCopy = *sysUtc;
        std::time_t sysUtcEpoch = std::mktime(&tmCopy);
        long systemTimezoneOffset = (long)(sysLocalEpoch - sysUtcEpoch);

        return localEpoch + systemTimezoneOffset - offsetSeconds;
    };

    while (std::getline(xmlFile, xmlLine)) {
        // A. Identify program block start boundaries
        size_t progPos = xmlLine.find("<programme start=\"");
        if (progPos != std::string::npos) {
            titleText = "";
            descText = "";
            
            size_t sStart = progPos + 18;
            size_t sEnd = xmlLine.find("\"", sStart);
            if (sEnd != std::string::npos) {
                progStartRaw = xmlLine.substr(sStart, sEnd - sStart);
            }

            size_t stopPos = xmlLine.find("stop=\"");
            if (stopPos != std::string::npos) {
                size_t eEnd = xmlLine.find("\"", stopPos + 6);
                if (eEnd != std::string::npos) {
                    progEndRaw = xmlLine.substr(stopPos + 6, eEnd - (stopPos + 6));
                }
            }

            size_t chanAttrPos = xmlLine.find("channel=\"");
            if (chanAttrPos != std::string::npos) {
                size_t cStart = chanAttrPos + 9;
                size_t cEnd = xmlLine.find("\"", cStart);
                if (cEnd != std::string::npos) {
                    progChanId = xmlLine.substr(cStart, cEnd - cStart);
                }
            }
            continue;
        }

        // B. Accumulate metadata attributes while inside the open block stream context
        if (!progChanId.empty()) {
            size_t titlePos = xmlLine.find("<title");
            if (titlePos != std::string::npos) {
                size_t valStart = xmlLine.find(">", titlePos) + 1;
                size_t valEnd = xmlLine.find("</title>", valStart);
                if (valEnd != std::string::npos) {
                    titleText = xmlLine.substr(valStart, valEnd - valStart);
                    
                    // =========================================================================
                    // 🧽 CORE XML TEXT ENTITY SANITIZATION (FIXES AMPERSANDS SYSTEM-WIDE)
                    // =========================================================================
                    size_t pos;
                    while ((pos = titleText.find("&amp;")) != std::string::npos) { titleText.replace(pos, 5, "&"); }
                    while ((pos = titleText.find("&quot;")) != std::string::npos) { titleText.replace(pos, 6, "\""); }
                    while ((pos = titleText.find("&apos;")) != std::string::npos) { titleText.replace(pos, 6, "'"); }
                    while ((pos = titleText.find("&lt;")) != std::string::npos) { titleText.replace(pos, 4, "<"); }
                    while ((pos = titleText.find("&gt;")) != std::string::npos) { titleText.replace(pos, 4, ">"); }
                    // =========================================================================
                }
            }

            size_t descPos = xmlLine.find("<desc");
            if (descPos != std::string::npos) {
                size_t valStart = xmlLine.find(">", descPos) + 1;
                size_t valEnd = xmlLine.find("</desc>", valStart);
                if (valEnd != std::string::npos) {
                    descText = xmlLine.substr(valStart, valEnd - valStart);
                    
                    size_t pos;
                    while ((pos = descText.find("&amp;")) != std::string::npos) { descText.replace(pos, 5, "&"); }
                    while ((pos = descText.find("&quot;")) != std::string::npos) { descText.replace(pos, 6, "\""); }
                    while ((pos = descText.find("&apos;")) != std::string::npos) { descText.replace(pos, 6, "'"); }
                }
            }
        }


        // C. Evaluate block layout parameters only when the node explicitly terminates
        if (xmlLine.find("</programme>") != std::string::npos) {
            std::string associatedChNum = xmlIdToChannelNumMap[progChanId];

            if (!associatedChNum.empty() && !titleText.empty()) {
                std::time_t progStartEpoch = parseXmlTimeToEpoch(progStartRaw);
                std::time_t progEndEpoch   = parseXmlTimeToEpoch(progEndRaw);

                auto& activeItem = cloudGuideMap[associatedChNum];

                for (int32 bucket = 0; bucket < 4; bucket++) {
                    std::time_t bucketTargetEpoch = targetComparisonEpoch + (bucket * 30 * 60);

                    if (bucketTargetEpoch >= progStartEpoch && bucketTargetEpoch < progEndEpoch) {
                        
                        std::time_t displayLocalEpoch = bucketTargetEpoch;
                        std::tm* displayTime = std::localtime(&displayLocalEpoch);
                        char timeBuf[32] = {0};
                        std::strftime(timeBuf, sizeof(timeBuf), "%I:%M %p", displayTime);

                        BString formattedTimeStr(timeBuf);
                        if (formattedTimeStr.StartsWith("0")) {
                            formattedTimeStr.Remove(0, 1);
                        }

                        std::time_t endEpochCopy = progEndEpoch;
                        std::tm* endTm = std::localtime(&endEpochCopy);
                        char endBuf[32] = {0};
                        std::strftime(endBuf, sizeof(endBuf), "%I:%M %p", endTm);
                        BString formattedEndTimeStr(endBuf);
                        if (formattedEndTimeStr.StartsWith("0")) {
                            formattedEndTimeStr.Remove(0, 1);
                        }
                        
                        if (bucket == 0) {
                            activeItem.nowPlaying = titleText;
                            activeItem.nowPlayingDurationMinutes = (int32)((progEndEpoch - progStartEpoch) / 60);
                            
                            activeItem.nowPlayingEndTimeStr = formattedEndTimeStr.String();
                            activeItem.nowPlayingDescription = descText;
                        } else {

                            while ((int32)activeItem.futureLineup.size() < bucket) {
                                UpcomingShowItem placeholder;
                                placeholder.title = "No Programming Data Available";                                    
                                std::time_t placeholderEpoch = targetComparisonEpoch + ((activeItem.futureLineup.size() + 1) * 30 * 60);
                                std::tm* pTime = std::localtime(&placeholderEpoch);
                                char pBuf[32] = {0};
                                std::strftime(pBuf, sizeof(pBuf), "%I:%M %p", pTime);
                                
                                BString formattedPTime(pBuf);
                                if (formattedPTime.StartsWith("0")) {
                                    formattedPTime.Remove(0, 1);
                                }
                                placeholder.startTimeStr = formattedPTime.String();
                                placeholder.endTimeStr = "";
                                placeholder.description = "";
                                placeholder.durationMinutes = 30;

                                activeItem.futureLineup.push_back(placeholder);
                            }

                            UpcomingShowItem futureShow;
                            futureShow.title = titleText;
                            futureShow.durationMinutes = (int32)((progEndEpoch - progStartEpoch) / 60);
                            futureShow.startTimeStr = formattedTimeStr.String();
                            futureShow.endTimeStr = formattedEndTimeStr.String();
                            futureShow.description = descText.c_str(); 

                            activeItem.futureLineup[bucket - 1] = futureShow;
                        }
                    }
                }
            }
            progChanId = "";
            progStartRaw = "";
            progEndRaw = "";
            titleText = "";
            descText = "";
        }
    }
    xmlFile.close();


    // -------------------------------------------------------------------------
    // PASS 3: FILTER SELECTIONS AGAINST PHYSICAL HDHOMERUN LINEUP TUNERS
    // -------------------------------------------------------------------------
    std::string lineupUrl = "http://" + targetIp + "/lineup.json";
    std::string lineupPayload;
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 HaikuDVR/1.0");
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
                    if (finalItem.guideName.empty()) {
                        finalItem.guideName = channelEntry.value("GuideName", "Unknown");
                    }
                } else {
                    finalItem.guideNumber = chNum;
                    finalItem.guideName   = channelEntry.value("GuideName", "Unknown");
                    finalItem.nowPlaying  = "Live Stream Available";
                }

                int32 activeListRowIndex = (int32)fLoadedChannels.size();
                fLoadedChannels.push_back(finalItem);

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
        fStatusLabel->SetText("Status: Local tuner lineup filter processing failed.");
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

        // Restored your exact naming paths to align with Pass 3's real-time downloads
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
            std::string shortDate = (rawDate.length() >= 10) ? rawDate.substr(5) : rawDate;
            timeContextLabel = "On " + shortDate + ": ";
        }

        std::string displayLabel = item.guideNumber + " - " + item.guideName + " (" + timeContextLabel + item.nowPlaying + ") ";
        
        // --- End of Pass 4 List View Addition Loop ---
        fChannelListView->AddItem(new ChannelListItem(displayLabel.c_str(), activeIcon));
    }
    std::fflush(stdout); 

    if (gIconWindowMessenger == nullptr) {
        gIconWindowMessenger = new BMessenger(this);
    }

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
        
        // Refresh every 5 minutes       
        fLastNetworkSyncTime = 0; 
        fRefreshRunner = new BMessageRunner(BMessenger(this), 
            new BMessage(MSG_PERIODIC_GUIDE_REFRESH), 
            300000000LL, -1); 


        new BMessageRunner(BMessenger(this), new BMessage('TICK'), 60000000LL, -1);


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

        optionsMenu->AddSeparatorItem(); 

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
        
        optionsMenu->AddSeparatorItem(); 
        
        BMessage* msgOpenGuide = new BMessage(MSG_OPEN_GUIDE);
        BMenuItem* guideItem = new BMenuItem("Open Guide...", msgOpenGuide);
        optionsMenu->AddItem(guideItem);
        
        optionsMenu->AddSeparatorItem();
		BMenuItem* firmwareItem = new BMenuItem("Check Tuner Firmware...", new BMessage(MSG_CHECK_FIRMWARE));
		optionsMenu->AddItem(firmwareItem);
        
        // =========================================================================
        // DEFAULT PLAYER RADIO SELECTION SECTION
        // =========================================================================
        optionsMenu->AddSeparatorItem();
        
        BMenuItem* playerHeader = new BMenuItem("--- Default Player ---", NULL);
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
        
        fScheduleButton = new BButton(BRect(20, 300, 330, 335), "schedule", "Queue Scheduled Show", new BMessage(MSG_ADD_SCHEDULE));
  
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
        fRestartBackendButton = new BButton(BRect(730, 375, 860, 405), "restart_backend", "ABORT!", new BMessage(MSG_RESTART_BACKEND));
        fRestartBackendButton->SetToolTip("Warning: ABORT! will immediately abort any active scheduled recording streams currently in progress!");

        BBox* statusBox = new BBox(BRect(730, 415, 860, 445), "bebox_status_wrapper");
        statusBox->SetBorder(B_FANCY_BORDER); 

        fBackendStatusLabel = new BStringView(BRect(5, 5, 125, 25), "backend_status", "Backend: Checking...");
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
        	
        	
       case MSG_TOGGLE_DEBUG: {
            bool enableDebug = true;
            if (message->FindBool("enable", &enableDebug) == B_OK) {
                cfg.debugEnable = enableDebug;
                
                fDebugOnItem->SetMarked(cfg.debugEnable == true);
                fDebugOffItem->SetMarked(cfg.debugEnable == false);
                
                SaveSchedulesToDisk();
                
                printf("[DEBUG_SYS] System logging runtime state mutated via UI: %s\n", 
                       cfg.debugEnable ? "ENABLED" : "DISABLED");
            }
            break;
        }
       	
        case MSG_TOGGLE_NOTIFICATIONS: {
            bool enableAlerts = true;
            if (message->FindBool("enable", &enableAlerts) == B_OK) {
                cfg.showUpdateNotifications = enableAlerts;
                
                fNotifyOnItem->SetMarked(cfg.showUpdateNotifications == true);
                fNotifyOffItem->SetMarked(cfg.showUpdateNotifications == false);
                
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
             fDateInput->SetText(newDateString);
             FetchAndPopulateChannelList(newDateString);

             BListView* realGuideList = dynamic_cast<BListView*>(FindView("guide_list_view"));             
             if (realGuideList != nullptr) {
                 realGuideList->MakeEmpty(); 
                 realGuideList->Invalidate(); 
             }

             if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
                 BMessage refreshMessage(MSG_DATE_SELECTED);
                 fGuideWindow->PostMessage(&refreshMessage);
                 
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

		case MSG_CLOCK_UP:
		case MSG_CLOCK_DOWN: {
		    std::string timeStr = fTimeInput->Text();
		    size_t colonPos = timeStr.find(':');
		    if (colonPos != std::string::npos) {
		        int hours = std::atoi(timeStr.substr(0, colonPos).c_str());
		        int minutes = std::atoi(timeStr.substr(colonPos + 1).c_str());
		        
		        if (message->what == MSG_CLOCK_UP) {
		            minutes += 30;
		        } else {
		            minutes -= 30;
		        }
		        
		        if (minutes >= 60) { minutes = 0; hours++; }
		        if (minutes < 0) { minutes = 30; hours--; } 
		        if (hours >= 24) { hours = 0; }
		        if (hours < 0) { hours = 23; }
		        
		        char updatedTimeBuffer[16];
		        sprintf(updatedTimeBuffer, "%02d:%02d", hours, minutes);
		        fTimeInput->SetText(updatedTimeBuffer);
		
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


         case MSG_CHANNEL_CLICKED: {
             int32 selection = fChannelListView->CurrentSelection();
             if (selection >= 0 && (size_t)selection < fLoadedChannels.size()) {
                 const auto& channel = fLoadedChannels[selection];
                 
                 fChannelInput->SetText(channel.guideNumber.c_str());
                 
                 std::string guidePreview = "Lineup for " + channel.guideName + ": ";
                 if (!channel.futureLineup.empty()) {
                     for (size_t s = 0; s < channel.futureLineup.size(); s++) {
                         BString cleanTitle(channel.futureLineup[s].title.c_str());
                         
                         cleanTitle.ReplaceAll("&amp;", "&");
                         cleanTitle.ReplaceAll("&lt;", "<");
                         cleanTitle.ReplaceAll("&gt;", ">");
                         cleanTitle.ReplaceAll("&quot;", "\"");
                         cleanTitle.ReplaceAll("&apos;", "'");

                         guidePreview += "[";
                         guidePreview += channel.futureLineup[s].startTimeStr;
                         guidePreview += "] ";
                         guidePreview += cleanTitle.String();
                                      
                         if (s < channel.futureLineup.size() - 1) {
                             guidePreview += "  |  ";
                         }
                     }
                 } else {
                     guidePreview += "No upcoming schedule data available.";
                 }
                 
                 BString finalCleanPreview(guidePreview.c_str());
                 finalCleanPreview.ReplaceAll("&amp;", "&");
                 
                 fStatusLabel->SetText(finalCleanPreview.String());
                 fStatusLabel->SetFont(be_bold_font);
                 
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
                 // --- ADD hTV OPTION MATCHING ---
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
            message->FindString("channel_label", &channelLabel) == B_OK &&
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

                if (rawTime.length() == 5 && rawTime.at(0) == '0') {
                    // uncomment the line below if your database expects "5:30" instead of "05:30"
                    // rawTime = rawTime.substr(1); 
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

                size_t sPos = fSelectedDurationSeconds.find('s');
                if (sPos != std::string::npos) {
                    fSelectedDurationSeconds = fSelectedDurationSeconds.substr(0, sPos); 
                }

                ScheduleItem item;
                item.startDate = fDateInput->Text(); 
                item.startTime = rawTime; 
                item.channel = fChannelInput->Text();
                item.duration = fSelectedDurationSeconds; 
                item.showTitle = showTitle.String(); 
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

        BString cleanTitle = extractedTitle;
        cleanTitle.ReplaceAll("/", "-");
        cleanTitle.ReplaceAll(":", "-");
        cleanTitle.ReplaceAll("\\", "-");
        cleanTitle.ReplaceAll("*", "");
        cleanTitle.ReplaceAll("?", "");
        cleanTitle.ReplaceAll(" ", "_"); 
        cleanTitle.Trim();

        if (cleanTitle.IsEmpty() || cleanTitle == "Manual_Recording") {
            item.showTitle = "Manual_Recording";
        } else {
            if (!cleanTitle.StartsWith("Manual_Recording")) {
                cleanTitle.Prepend("Manual_Recording_");
            }
            item.showTitle = cleanTitle.String();
        }

        BString channelLabel;
        if (message->FindString("channel_label", &channelLabel) == B_OK) {
            item.channelLabel = channelLabel.String();
        } else {
            item.channelLabel = "Ch_" + item.channel;
        }

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

         BString extractedTitle;
         if (fChannelListView != nullptr) {
             int32 selectedIndex = fChannelListView->CurrentSelection();
             if (selectedIndex >= 0) {
                 ChannelListItem* listItem = (ChannelListItem*)fChannelListView->ItemAt(selectedIndex);
                 if (listItem != nullptr) {
                     BString listRowText(listItem->textDisplay.c_str()); // e.g. "2.1 - WSBDT (Now: Channel 2 Action News)"
                     
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


         BString extractedLabel;
         if (message->FindString("channel_label", &extractedLabel) == B_OK) {
             config->channelLabel = extractedLabel.String();
         } else {
             config->channelLabel = "Ch_" + config->channel;
         }

         BString cleanTitle = extractedTitle;
         cleanTitle.ReplaceAll("/", "-");
         cleanTitle.ReplaceAll(":", "-");
         cleanTitle.ReplaceAll("\\", "-");
         cleanTitle.ReplaceAll("*", "");
         cleanTitle.ReplaceAll("?", "");
         cleanTitle.ReplaceAll(" ", "_"); 
         cleanTitle.Trim();

         if (cleanTitle.IsEmpty() || cleanTitle == "Manual_Recording") {
             config->showTitle = "Manual_Recording";
         } else {
             if (!cleanTitle.StartsWith("Manual_Recording")) {
                 cleanTitle.Prepend("Manual_Recording_");
             }
             config->showTitle = cleanTitle.String();
         }

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
             
             char dateBuffer[32];
             std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", timeInfo);
             
             char timeBuffer[32];
             std::strftime(timeBuffer, sizeof(timeBuffer), "%H-%M-%S", timeInfo);

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

            if (fLastNetworkSyncTime == 0 || (currentTime - fLastNetworkSyncTime) >= 86400) {
                if (isViewingToday) {
                    FetchAndPopulateChannelList(); 
                }
                fLastNetworkSyncTime = currentTime;
                if (cfg.debugEnable) printf("[EPG SYNC] Master EPG reloaded from network.\n");
            } 
            
            if (isViewingToday) {
                struct tm* localTimeInfo = localtime(&currentTime);
                int systemAbsoluteMinutes = (localTimeInfo->tm_hour * 60) + localTimeInfo->tm_min;

                for (size_t i = 0; i < fLoadedChannels.size(); i++) {
                    auto& channel = fLoadedChannels[i];
                    
                    while (!channel.futureLineup.empty()) {
                        const auto& nextShow = channel.futureLineup.front();
                        
                        int showHour = 0, showMin = 0;
                        char ampm[16] = {0};
                        if (sscanf(nextShow.startTimeStr.c_str(), "%d:%d %15s", &showHour, &showMin, ampm) >= 2) {
                            std::string aStr(ampm);
                            if (aStr.find("PM") != std::string::npos || aStr.find("pm") != std::string::npos) {
                                if (showHour < 12) showHour += 12;
                            } else if (aStr.find("AM") != std::string::npos || aStr.find("am") != std::string::npos) {
                                if (showHour == 12) showHour = 0;
                            }
                            
                            int showStartAbsoluteMinutes = (showHour * 60) + showMin;
                            
                            if (systemAbsoluteMinutes >= showStartAbsoluteMinutes) {
                                channel.nowPlaying = nextShow.title; 
                                channel.futureLineup.erase(channel.futureLineup.begin()); 
                                continue;
                            }
                        }
                        break; 
                    }
                }
            }

            RefreshScheduleListView();        
            if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
                fGuideWindow->PostMessage(MSG_PERIODIC_GUIDE_REFRESH);
                fGuideWindow->Unlock();
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
	                printf("[FRONTEND] Guide closed while Scheduler was hidden. Restoring Scheduler window view.\n");
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
		        
		        case 'TICK': { 		         
		            if (fGuideWindow != nullptr && fGuideWindow->Lock()) {
		                BView* childHeader = fGuideWindow->FindView("timelineHeader");
		                if (childHeader != nullptr) {
		                    childHeader->Invalidate(); 
		                }
		                fGuideWindow->Unlock();
		            }
		            break;
		        }

		
		       case MSG_SET_PLAYER_MPV: {
		            cfg.defaultPlayer = "MPV";
		            fPlayerMpvItem->SetMarked(true);
		            fPlayerMediaItem->SetMarked(false);
		            fPlayerVlcItem->SetMarked(false);
		            fPlayerHtvItem->SetMarked(false); // Unmark hTV
		            SaveSchedulesToDisk();
		            break;
		        }
		        
		       case MSG_SET_PLAYER_MEDIAPLAYER: {
		            cfg.defaultPlayer = "MediaPlayer";
		            fPlayerMpvItem->SetMarked(false);
		            fPlayerMediaItem->SetMarked(true);
		            fPlayerVlcItem->SetMarked(false);
		            fPlayerHtvItem->SetMarked(false); // Unmark hTV
		            SaveSchedulesToDisk();
		            break;
		        }
		        
		       case MSG_SET_PLAYER_VLC: {
		            cfg.defaultPlayer = "VLC";
		            fPlayerMpvItem->SetMarked(false);
		            fPlayerMediaItem->SetMarked(false);
		            fPlayerVlcItem->SetMarked(true);
		            fPlayerHtvItem->SetMarked(false); // Unmark hTV
		            SaveSchedulesToDisk();
		            break;
		        }

		       // --- ADD hTV SELECTION CASE BLOCK ---
		       case MSG_SET_PLAYER_HTV: {
		            cfg.defaultPlayer = "hTV";
		            fPlayerMpvItem->SetMarked(false);
		            fPlayerMediaItem->SetMarked(false);
		            fPlayerVlcItem->SetMarked(false);
		            fPlayerHtvItem->SetMarked(true); // Checkmark hTV exclusively
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

                int32 itemCount = fContainerList->CountItems();
                for (int32 i = itemCount - 1; i >= 0; i--) {
                    BListItem* item = fContainerList->RemoveItem(i);
                    delete item;
                }

                const std::vector<ChannelGuideItem>& freshChannels = mainWindow->GetLoadedChannels();

                _BuildGuideRowsFromLiveChannels(freshChannels, fMainChannelListView);

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
        this->Hide(); 
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
        window->PostMessage(MSG_OPEN_GUIDE);
    }

};


int main() {
	ensure_config_dir();
    DVRApplication app;
    app.Run();
    return 0;
}

             
