#ifndef RESOURCE_H
#define RESOURCE_H

#define IDI_APP_ICON        101

/* Menu IDs */
#define IDM_FILE_OPEN       40001
#define IDM_FILE_LOADPRI    40002
#define IDM_FILE_LOADSEC    40003
#define IDM_FILE_LOADTER    40004
#define IDM_FILE_EXIT       40005
#define IDM_PLAY_TOGGLE     40010
#define IDM_PLAY_STOP       40011
#define IDM_PLAY_FWD5       40012
#define IDM_PLAY_BACK5      40013

/* Child Control IDs */
#define IDC_BTN_OPEN        1001
#define IDC_BTN_PLAYPAUSE   1002
#define IDC_BTN_STOP        1003
#define IDC_SLIDER_SEEK     1004
#define IDC_LABEL_TIME      1005
#define IDC_LABEL_VOL       1006
#define IDC_SLIDER_VOL      1007
#define IDC_CHK_REMEMBER    1008

#define IDC_LABEL_SUB_PRI   1010
#define IDC_COMBO_SUB_PRI   1011
#define IDC_BTN_LOAD_PRI    1012

#define IDC_LABEL_SUB_SEC   1020
#define IDC_COMBO_SUB_SEC   1021
#define IDC_BTN_LOAD_SEC    1022

#define IDC_LABEL_SUB_TER   1030
#define IDC_COMBO_SUB_TER   1031
#define IDC_BTN_LOAD_TER    1032

/* Timer ID */
#define IDT_PLAYBACK_TIMER  1

#endif /* RESOURCE_H */
