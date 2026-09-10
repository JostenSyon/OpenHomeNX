#pragma once
#include <string>
#include <vector>

namespace i18n {
    void init(const std::string& lang);
    const std::string& get(const char* key);
    std::string fmt(const char* key, const std::vector<std::string>& args);
    const std::string& currentLang();
    std::vector<std::string> availableLangs();
    void clearCache();

    // Convenience overloads for fmt
    inline std::string fmt(const char* key, const std::string& a0) {
        return fmt(key, std::vector<std::string>{a0});
    }
    inline std::string fmt(const char* key, const std::string& a0, const std::string& a1) {
        return fmt(key, std::vector<std::string>{a0, a1});
    }
    inline std::string fmt(const char* key, const std::string& a0, const std::string& a1, const std::string& a2) {
        return fmt(key, std::vector<std::string>{a0, a1, a2});
    }
    inline std::string fmt(const char* key, const std::string& a0, const std::string& a1, const std::string& a2, const std::string& a3) {
        return fmt(key, std::vector<std::string>{a0, a1, a2, a3});
    }
}

// Language display names (for selector popup)
struct LangInfo {
    const char* code;
    const char* displayName;
};

// NOTE: Korean and Chinese are excluded — PlSharedFontType_Standard does not include
// CJK/Korean glyphs. Supporting these requires loading PlSharedFontType_KO /
// PlSharedFontType_ChineseSimplified / PlSharedFontType_ChineseTraditional.
// Translation files (ko.json, zh-Hans.json, zh-Hant.json) are present in romfs
// and can be enabled once font loading supports these scripts.
inline const LangInfo KNOWN_LANGS[] = {
    {"en",      "English"},
    {"ja",      "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},       // 日本語
    {"fr",      "Fran\xc3\xa7" "ais"},                            // Français
    {"de",      "Deutsch"},
    {"es",      "Espa\xc3\xb1ol"},                                // Español
    {"it",      "Italiano"},
    {"nl",      "Nederlands"},
    {"pt",      "Portugu\xc3\xaas"},                                    // Português
    {"ru",      "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"}, // Русский
};
inline constexpr int KNOWN_LANG_COUNT = sizeof(KNOWN_LANGS) / sizeof(KNOWN_LANGS[0]);

inline const char* langDisplayName(const std::string& code) {
    for (int i = 0; i < KNOWN_LANG_COUNT; i++) {
        if (code == KNOWN_LANGS[i].code) return KNOWN_LANGS[i].displayName;
    }
    return code.c_str();
}

// --- String Keys ---

namespace StrKey {
    // ui.cpp - dialogs & modals
    constexpr const char* PressBToDismiss     = "press_b_dismiss";
    constexpr const char* AContinueBCancel     = "a_continue_b_cancel";
    constexpr const char* LoadingGameIcons     = "loading_game_icons";
    constexpr const char* LoadingProfiles      = "loading_profiles";
    constexpr const char* Saving               = "saving";
    constexpr const char* LoadingSaveData      = "loading_save_data";
    constexpr const char* MountError           = "mount_error";
    constexpr const char* FailedMountSave      = "failed_mount_save";
    constexpr const char* LowStorage           = "low_storage";
    constexpr const char* LowStorageBody       = "low_storage_body";
    constexpr const char* BackupFailed         = "backup_failed";
    constexpr const char* BackupFailedBody     = "backup_failed_body";
    constexpr const char* RoundTripCheck       = "round_trip_check";

    // ui_bank.cpp - bank selector
    constexpr const char* AllBanks             = "all_banks";
    constexpr const char* SelectSideBank       = "select_side_bank";
    constexpr const char* SelectBank           = "select_bank";
    constexpr const char* NoBanksFound         = "no_banks_found";
    constexpr const char* PressXCreate         = "press_x_create";
    constexpr const char* StatusBankAll        = "status_bank_all";
    constexpr const char* StatusBankNormal     = "status_bank_normal";
    constexpr const char* DeleteBankConfirm    = "delete_bank_confirm";
    constexpr const char* CannotUndo           = "cannot_undo";
    constexpr const char* AConfirmBCancel      = "a_confirm_b_cancel";
    constexpr const char* CannotDelete         = "cannot_delete";
    constexpr const char* BankCurrentlyLoaded  = "bank_currently_loaded";
    constexpr const char* DeletingBank         = "deleting_bank";
    constexpr const char* EnterBankName        = "enter_bank_name";
    constexpr const char* RenameBank           = "rename_bank";
    constexpr const char* RenameBox            = "rename_box";
    constexpr const char* SpeciesNameInput     = "species_name_input";
    constexpr const char* OtNameInput          = "ot_name_input";
    constexpr const char* MinLevel             = "min_level";
    constexpr const char* MaxLevel             = "max_level";
    constexpr const char* NotEnoughSpace       = "not_enough_space";
    constexpr const char* FreeNeedSpace        = "free_need_space";
    constexpr const char* CreatingBank         = "creating_bank";
    constexpr const char* RenamingBank         = "renaming_bank";
    constexpr const char* AlreadyOpen          = "already_open";
    constexpr const char* BankAlreadyRight     = "bank_already_right";
    constexpr const char* BankAlreadyLeft      = "bank_already_left";
    constexpr const char* LoadingBank          = "loading_bank";
    constexpr const char* NoBankLoaded         = "no_bank_loaded";
    constexpr const char* InvalidBankFile      = "invalid_bank_file";
    constexpr const char* BankNameExists       = "bank_name_exists";
    constexpr const char* BankNameExistsBody   = "bank_name_exists_body";

    // ui_selectors.cpp
    constexpr const char* SelectProfile        = "select_profile";
    constexpr const char* StatusProfile        = "status_profile";
    constexpr const char* NoSaveData           = "no_save_data";
    constexpr const char* NoSaveDataBody       = "no_save_data_body";
    constexpr const char* SelectGameDual       = "select_game_dual";
    constexpr const char* DualBankHint         = "dual_bank_hint";
    constexpr const char* SelectGame           = "select_game";
    constexpr const char* ViewAllBanks         = "view_all_banks";
    constexpr const char* StatusGameBackPage   = "status_game_back_page";
    constexpr const char* StatusGameBack       = "status_game_back";
    constexpr const char* StatusGameQuitPage   = "status_game_quit_page";
    constexpr const char* StatusGameQuit       = "status_game_quit";
    constexpr const char* StatusGameEject      = "status_game_eject";
    constexpr const char* DualBankMode         = "dual_bank_mode";
    constexpr const char* NoBanksTitle         = "no_banks_title";
    constexpr const char* NoBanksAnyGame       = "no_banks_any_game";
    constexpr const char* NoBoxesTitle         = "no_boxes_title";
    constexpr const char* NoBoxesBody          = "no_boxes_body";

    // ui_input.cpp
    constexpr const char* ExportComplete       = "export_complete";
    constexpr const char* PokemonExported      = "pokemon_exported";
    constexpr const char* ExportFailedCount    = "export_failed_count";
    constexpr const char* NoBanksAvailable     = "no_banks_available";
    constexpr const char* CreateNewBank        = "create_new_bank";
    constexpr const char* PartyPokemon         = "party_pokemon";
    constexpr const char* CantReleaseParty     = "cant_release_party";
    constexpr const char* CantEmptyParty       = "cant_empty_party";
    constexpr const char* EmptyPartyTitle      = "empty_party_title";
    constexpr const char* EmptyPartyCaterpie   = "empty_party_caterpie";
    constexpr const char* EmptyPartyMagikarp   = "empty_party_magikarp";
    constexpr const char* QuitHoldTitle        = "quit_hold_title";
    constexpr const char* QuitHoldBody         = "quit_hold_body";
    constexpr const char* ReleasePokemon       = "release_pokemon";
    constexpr const char* ReleaseConfirm       = "release_confirm";
    constexpr const char* Exported             = "exported";
    constexpr const char* ExportFailed         = "export_failed";
    constexpr const char* CouldNotWrite        = "could_not_write";
    constexpr const char* ReleaseMultiConfirm  = "release_multi_confirm";
    constexpr const char* CantMovePartyBank    = "cant_move_party_bank";
    constexpr const char* SlotsOccupied        = "slots_occupied";
    constexpr const char* SlotsOccupiedBody    = "slots_occupied_body";
    constexpr const char* NotEnoughSpaceSlots  = "not_enough_space_slots";
    constexpr const char* NeedEmptySlots       = "need_empty_slots";
    constexpr const char* InvalidWC            = "invalid_wc";
    constexpr const char* InvalidWCBody        = "invalid_wc_body";
    constexpr const char* CannotInject         = "cannot_inject";
    constexpr const char* CannotInjectBody     = "cannot_inject_body";
    constexpr const char* CannotInjectBank     = "cannot_inject_bank";
    constexpr const char* CannotInjectBankBody = "cannot_inject_bank_body";
    constexpr const char* SlotOccupied         = "slot_occupied";
    constexpr const char* SlotOccupiedBody     = "slot_occupied_body";
    constexpr const char* Error                = "error";
    constexpr const char* FailedLoadWC         = "failed_load_wc";
    constexpr const char* Injected             = "injected";
    constexpr const char* InjectedBody         = "injected_body";

    // ui_render.cpp - detail popup
    constexpr const char* LvPrefix             = "lv_prefix";
    constexpr const char* StatHP               = "stat_hp";
    constexpr const char* StatAtk              = "stat_atk";
    constexpr const char* StatDef              = "stat_def";
    constexpr const char* StatSpe              = "stat_spe";
    constexpr const char* StatSpD              = "stat_spd";
    constexpr const char* StatSpA              = "stat_spa";
    constexpr const char* NationalDexPrefix    = "national_dex_prefix";
    constexpr const char* OTPrefix             = "ot_prefix";
    constexpr const char* HTPrefix             = "ht_prefix";
    constexpr const char* TIDPrefix            = "tid_prefix";
    constexpr const char* SIDPrefix            = "sid_prefix";
    constexpr const char* NaturePrefix         = "nature_prefix";
    constexpr const char* AbilityPrefix        = "ability_prefix";
    constexpr const char* HeldItemPrefix       = "held_item_prefix";
    constexpr const char* NoneItem             = "none_item";
    constexpr const char* Moves                = "moves";
    constexpr const char* RibbonsMarks         = "ribbons_marks";
    constexpr const char* MoreRibbons          = "more_ribbons";
    constexpr const char* IVs                  = "ivs";
    constexpr const char* EVs                  = "evs";
    constexpr const char* DetailFooter         = "detail_footer";

    // ui_render.cpp - menu popup
    constexpr const char* MenuTitle            = "menu_title";
    constexpr const char* MenuTheme            = "menu_theme";
    constexpr const char* MenuLanguage         = "menu_language";
    constexpr const char* MenuSearch           = "menu_search";
    constexpr const char* MenuWondercard       = "menu_wondercard";
    constexpr const char* MenuExportSelected   = "menu_export_selected";
    constexpr const char* MenuImportPk         = "menu_import_pk";
    constexpr const char* MenuSwitchBank       = "menu_switch_bank";
    constexpr const char* MenuChangeGame       = "menu_change_game";
    constexpr const char* MenuSaveQuit         = "menu_save_quit";
    constexpr const char* MenuQuitNoSave       = "menu_quit_no_save";
    constexpr const char* MenuSwitchLeft       = "menu_switch_left";
    constexpr const char* MenuSwitchRight      = "menu_switch_right";
    constexpr const char* MenuSaveBanks        = "menu_save_banks";
    constexpr const char* MenuQuit             = "menu_quit";
    constexpr const char* AConfirmBCancelMenu  = "a_confirm_b_cancel_menu";

    // ui_render.cpp - theme selector
    constexpr const char* SelectTheme          = "select_theme";
    constexpr const char* ASelectBCancel       = "a_select_b_cancel";
    constexpr const char* ImportSettingsTitle  = "import_settings_title";
    constexpr const char* ImportAddPath        = "import_add_path";
    constexpr const char* ImportSettingsFooter = "import_settings_footer";
    constexpr const char* FolderBrowserTitle   = "folderbrowser_title";
    constexpr const char* FolderBrowserFooter  = "folderbrowser_footer";
    constexpr const char* FolderBrowserAdded   = "folderbrowser_added";
    constexpr const char* FolderBrowserExists  = "folderbrowser_exists";
    constexpr const char* ImportPathInputHdr   = "import_path_input_hdr";

    // settings page (game selector gear)
    constexpr const char* SetTitle        = "set_title";
    constexpr const char* SetAppearance   = "set_appearance";
    constexpr const char* SetEngine       = "set_engine";
    constexpr const char* SetData         = "set_data";
    constexpr const char* SetUpdate       = "set_update";
    constexpr const char* SetDebug        = "set_debug";
    constexpr const char* SetInfo         = "set_info";
    constexpr const char* SetUser         = "set_user";
    constexpr const char* SetDefaultUser  = "set_defaultuser";
    constexpr const char* SetUserAsk      = "set_user_ask";
    constexpr const char* SetTheme        = "set_theme";
    constexpr const char* SetLanguage     = "set_language";
    constexpr const char* SetZoom         = "set_zoom";
    constexpr const char* SetCore         = "set_core";
    constexpr const char* SetCoreOh       = "set_core_oh";
    constexpr const char* SetCorePk       = "set_core_pk";
    constexpr const char* SetTargetGen    = "set_targetgen";
    constexpr const char* SetSavePaths    = "set_savepaths";
    constexpr const char* SetScan         = "set_scan";
    constexpr const char* SetScanDone     = "set_scan_done";
    constexpr const char* SetDbgMenu      = "set_dbgmenu";
    constexpr const char* SetOpen         = "set_open";
    constexpr const char* SetBackupMax    = "set_backupmax";
    constexpr const char* SetBackupClean  = "set_backupclean";
    constexpr const char* SetCleanConfirm = "set_clean_confirm";
    constexpr const char* SetCleanDone    = "set_clean_done";
    constexpr const char* SetCheckUpdate  = "set_checkupdate";
    constexpr const char* SetSource       = "set_source";
    constexpr const char* SetEditUrl      = "set_editurl";
    constexpr const char* SetEditUrlHdr   = "set_editurl_hdr";
    constexpr const char* SetDebugToggle  = "set_debugtoggle";
    constexpr const char* SetOn           = "set_on";
    constexpr const char* SetOff          = "set_off";
    constexpr const char* SetVersion      = "set_version";
    constexpr const char* SetCredits      = "set_credits";
    constexpr const char* BagSoon         = "bag_soon";
    constexpr const char* SetFooter       = "set_footer";
    constexpr const char* ImportFoundTitle     = "import_found_title";
    constexpr const char* ImportAutoCheckUsb   = "import_autocheck_usb";

    // ui_render.cpp - language selector
    constexpr const char* SelectLanguage       = "select_language";

    // ui_render.cpp - search/filter
    constexpr const char* SearchFilter         = "search_filter";
    constexpr const char* FilterSpecies        = "filter_species";
    constexpr const char* FilterAny            = "filter_any";
    constexpr const char* FilterOT             = "filter_ot";
    constexpr const char* FilterShiny          = "filter_shiny";
    constexpr const char* FilterYes            = "filter_yes";
    constexpr const char* FilterOff            = "filter_off";
    constexpr const char* FilterEgg            = "filter_egg";
    constexpr const char* FilterAlpha          = "filter_alpha";
    constexpr const char* FilterGender         = "filter_gender";
    constexpr const char* GenderAny            = "gender_any";
    constexpr const char* GenderMale           = "gender_male";
    constexpr const char* GenderFemale         = "gender_female";
    constexpr const char* GenderGenderless     = "gender_genderless";
    constexpr const char* FilterLevel          = "filter_level";
    constexpr const char* FilterPerfectIVs     = "filter_perfect_ivs";
    constexpr const char* IVOnePlus            = "iv_one_plus";
    constexpr const char* IVSix                = "iv_six";
    constexpr const char* FilterRibbons        = "filter_ribbons";
    constexpr const char* RibbonHasRibbon      = "ribbon_has_ribbon";
    constexpr const char* RibbonHasMark        = "ribbon_has_mark";
    constexpr const char* RibbonHasAny         = "ribbon_has_any";
    constexpr const char* FilterMode           = "filter_mode";
    constexpr const char* ModeListOn           = "mode_list_on";
    constexpr const char* ModeListOff          = "mode_list_off";
    constexpr const char* ModeHighlightOn      = "mode_highlight_on";
    constexpr const char* ModeHighlightOff     = "mode_highlight_off";
    constexpr const char* FilterReset          = "filter_reset";
    constexpr const char* FilterSearch         = "filter_search_btn";
    constexpr const char* FilterFooter         = "filter_footer";

    // ui_render.cpp - search results
    constexpr const char* SearchResultsTitle   = "search_results_title";
    constexpr const char* NoPokemonFound       = "no_pokemon_found";
    constexpr const char* BadgeShiny           = "badge_shiny";
    constexpr const char* BadgeAlpha           = "badge_alpha";
    constexpr const char* BadgeEgg             = "badge_egg";
    constexpr const char* Egg                  = "egg";
    constexpr const char* LocLeft              = "loc_left";
    constexpr const char* LocRight             = "loc_right";
    constexpr const char* LocSave              = "loc_save";
    constexpr const char* LocBank              = "loc_bank";
    constexpr const char* BoxLabel             = "box_label";
    constexpr const char* SlotLabel            = "slot_label";
    constexpr const char* ResultsFooterEmpty   = "results_footer_empty";
    constexpr const char* ResultsFooter        = "results_footer";

    // ui_render.cpp - species picker
    constexpr const char* SelectLetter         = "select_letter";
    constexpr const char* ASelectBBack         = "a_select_b_back";
    constexpr const char* NoSpeciesFound       = "no_species_found";
    constexpr const char* SpeciesDashLetter    = "species_dash_letter";

    // ui_render.cpp - wondercard list
    constexpr const char* WondercardsTitle     = "wondercards_title";
    constexpr const char* NoWCFound            = "no_wc_found";
    constexpr const char* PlaceFilesIn         = "place_files_in";
    constexpr const char* BadgeInvalid         = "badge_invalid";
    constexpr const char* PlayerOTTag          = "player_ot_tag";
    constexpr const char* BClose               = "b_close";
    constexpr const char* WCFooter             = "wc_footer";

    // ui_render.cpp - about popup
    constexpr const char* AboutTitle           = "about_title";
    constexpr const char* AboutDesc1           = "about_desc1";
    constexpr const char* AboutDesc2           = "about_desc2";
    constexpr const char* SupportedGames       = "supported_games";
    constexpr const char* SupportedLGPE        = "supported_lgpe";
    constexpr const char* SupportedSwSh        = "supported_swsh";
    constexpr const char* SupportedBDSPLA      = "supported_bdsp_la";
    constexpr const char* SupportedSVZA        = "supported_sv_za";
    constexpr const char* SupportedFRLG        = "supported_frlg";
    constexpr const char* SupportedGB         = "supported_gb";
    constexpr const char* AboutBasedOn         = "about_based_on";
    constexpr const char* AboutBasedPKHouse    = "about_based_pkhouse";
    constexpr const char* AboutBasedOpenHome   = "about_based_openhome";
    constexpr const char* AboutBasedPKHeX      = "about_based_pkhex";
    constexpr const char* AboutBasedLibnx      = "about_based_libnx";
    constexpr const char* AboutBasedDevkitPro  = "about_based_devkitpro";
    constexpr const char* CreditPKHeX          = "credit_pkhex";
    constexpr const char* CreditJKSV           = "credit_jksv";
    constexpr const char* Controls             = "controls";
    constexpr const char* ControlsLine1        = "controls_line1";
    constexpr const char* ControlsLine2        = "controls_line2";
    constexpr const char* PressMinusBClose     = "press_minus_b_close";

    // ui_render.cpp - box view overlay
    constexpr const char* BoxViewLeft          = "box_view_left";
    constexpr const char* BoxViewSave          = "box_view_save";
    constexpr const char* BoxViewBank          = "box_view_bank";
    constexpr const char* BoxViewFooterRename  = "box_view_footer_rename";
    constexpr const char* BoxViewFooter        = "box_view_footer";

    // ui_render.cpp - main status bar
    constexpr const char* StatusMain           = "status_main";
    constexpr const char* StatusSearch         = "status_search";
    constexpr const char* StatusHoldingMulti   = "status_holding_multi";
    constexpr const char* StatusHoldingSingle  = "status_holding_single";
    constexpr const char* StatusDrag           = "status_drag";
    constexpr const char* StatusSelected       = "status_selected";
    constexpr const char* KeepPositions        = "keep_positions";
    constexpr const char* LabelAllBanks        = "label_all_banks";
    constexpr const char* LabelDualBank        = "label_dual_bank";
    constexpr const char* Left                 = "left";
    constexpr const char* Right                = "right";

    // Updater (ui_selectors.cpp checkForUpdate + Send log)
    constexpr const char* UpdateTitle          = "update_title";
    constexpr const char* UpdateNetOff         = "update_net_off";
    constexpr const char* UpdateContacting     = "update_contacting";
    constexpr const char* UpdateUnreachable    = "update_unreachable";
    constexpr const char* UpdateAvailNetTitle  = "update_avail_net_title";
    constexpr const char* UpdateAvailNetBody   = "update_avail_net_body";
    constexpr const char* UpdateDownloading    = "update_downloading";
    constexpr const char* UpdateDlFailed       = "update_dl_failed";
    constexpr const char* UpdateSameDbgTitle   = "update_same_dbg_title";
    constexpr const char* UpdateSameDbgBody    = "update_same_dbg_body";
    constexpr const char* UpdateLatestBody     = "update_latest_body";
    constexpr const char* UpdateNoBuildBody    = "update_no_build_body";
    constexpr const char* UpdateAvailTitle     = "update_avail_title";
    constexpr const char* UpdateAvailBody      = "update_avail_body";
    constexpr const char* UpdateSameTitle      = "update_same_title";
    constexpr const char* UpdateSameBody       = "update_same_body";
    constexpr const char* UpdateDowngradeTitle = "update_downgrade_title";
    constexpr const char* UpdateDowngradeBody  = "update_downgrade_body";
    constexpr const char* UpdateUpdating       = "update_updating";
    constexpr const char* UpdateCopyFailed     = "update_copy_failed";
    constexpr const char* UpdateUpdatingTo     = "update_updating_to";
    constexpr const char* UpdateReplaceFailed  = "update_replace_failed";
    constexpr const char* UpdateInstalled      = "update_installed";
    constexpr const char* SendLogTitle         = "sendlog_title";
    constexpr const char* SendLogNoUrl         = "sendlog_no_url";
    constexpr const char* SendLogNetOff        = "sendlog_net_off";
    constexpr const char* SendLogUploading     = "sendlog_uploading";
    constexpr const char* SendLogSent          = "sendlog_sent";
    constexpr const char* SendLogSentBoth      = "sendlog_sent_both";
    constexpr const char* SendLogFailed        = "sendlog_failed";
    constexpr const char* SendSaveTitle        = "sendsave_title";
    constexpr const char* SendSaveNoUrl        = "sendsave_no_url";
    constexpr const char* SendSaveNetOff       = "sendsave_net_off";
    constexpr const char* SendSaveUploading    = "sendsave_uploading";
    constexpr const char* SendSaveSent         = "sendsave_sent";
    constexpr const char* SendSaveFailed       = "sendsave_failed";
    constexpr const char* SaveFailedTitle      = "save_failed_title";
    constexpr const char* SaveFailedBody       = "save_failed_body";
    constexpr const char* SendSaveNoGame       = "sendsave_no_game";
    constexpr const char* SendSaveOnlyImported = "sendsave_only_imported";

    // Cross-gen transfer (ui_input.cpp prepareForPlacement + drop dialogs)
    constexpr const char* TransferTitle        = "transfer_title";
    constexpr const char* TransferNoBankLeft   = "transfer_no_bank_left";
    constexpr const char* TransferNeedOh       = "transfer_need_oh";
    constexpr const char* TransferNeedOhBank   = "transfer_need_oh_bank";
    constexpr const char* TransferCantReadSrc  = "transfer_cant_read_src";
    constexpr const char* TransferCantReadSrcXfer = "transfer_cant_read_src_xfer";
    constexpr const char* TransferBadRecord    = "transfer_bad_record";
    constexpr const char* TransferNoOhpkm      = "transfer_no_ohpkm";
    constexpr const char* TransferCantBuild    = "transfer_cant_build";
    constexpr const char* TransferBadOhpkm     = "transfer_bad_ohpkm";
    constexpr const char* TransferNotInDex     = "transfer_not_in_dex";
    constexpr const char* TransferNoBytes      = "transfer_no_bytes";
    constexpr const char* Gen1DropsTitle       = "gen1_drops_title";
    constexpr const char* Gen1DropsBody        = "gen1_drops_body";
    constexpr const char* ExportGenTitle       = "export_gen_title";
    constexpr const char* ExportGenBody        = "export_gen_body";
    constexpr const char* ImportPkTitle        = "import_pk_title";
    constexpr const char* ImportPkNeedBank     = "import_pk_need_bank";
    constexpr const char* PkImportTitle        = "pkimport_title";
    constexpr const char* PkImportNone         = "pkimport_none";
    constexpr const char* PkImportFooter       = "pkimport_footer";
    constexpr const char* LearnsetTitle        = "learnset_title";
    constexpr const char* LearnsetNone         = "learnset_none";

    // Bank creation (ui_bank.cpp)
    constexpr const char* CreateBankTitle      = "create_bank_title";
    constexpr const char* CreateBankBody       = "create_bank_body";

    // Applet mode info (ui.cpp)
    constexpr const char* AppletTitle          = "applet_title";
    constexpr const char* AppletBody           = "applet_body";

}
