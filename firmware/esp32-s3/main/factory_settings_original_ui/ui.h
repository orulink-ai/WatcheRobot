#ifndef WATCHER_FACTORY_SETTINGS_ORIGINAL_UI_H
#define WATCHER_FACTORY_SETTINGS_ORIGINAL_UI_H

#include "lvgl.h"
#include "factory_home_ui/ui.h"
#include "ui_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Keep the imported SquareLine Settings screens source-compatible with the
 * factory project while avoiding two extra custom font blobs. The built-in
 * Montserrat sizes are close enough for the Settings secondary pages and keep
 * the firmware image smaller.
 */
#define ui_font_fontbold26 lv_font_montserrat_26
#define ui_font_fbold24 lv_font_montserrat_24

#define lv_font_montserrat_26 lv_font_montserrat_24
#define lv_font_montserrat_28 ui_font_semibold28

void ui_Page_Set_screen_init(void);
void ui_Page_Slider_screen_init(void);
void ui_Page_About_screen_init(void);
void ui_Page_Swipe_screen_init(void);
void ui_Page_Sleep_screen_init(void);
void ui_Page_Connect_screen_init(void);
void ui_Page_Network_screen_init(void);

extern lv_obj_t *ui_Page_Set;
extern lv_obj_t *ui_Set_panel;
extern lv_obj_t *ui_setback;
extern lv_obj_t *ui_Image1;
extern lv_obj_t *ui_setbackt;
extern lv_obj_t *ui_setdown;
extern lv_obj_t *ui_setdownt;
extern lv_obj_t *ui_setapp;
extern lv_obj_t *ui_setappt;
extern lv_obj_t *ui_setwifi;
extern lv_obj_t *ui_setwifit;
extern lv_obj_t *ui_setble;
extern lv_obj_t *ui_setblet;
extern lv_obj_t *ui_setblesw;
extern lv_obj_t *ui_setvol;
extern lv_obj_t *ui_setvolt;
extern lv_obj_t *ui_setbri;
extern lv_obj_t *ui_setbrit;
extern lv_obj_t *ui_settime;
extern lv_obj_t *ui_settimt;
extern lv_obj_t *ui_setrgb;
extern lv_obj_t *ui_setrgbt;
extern lv_obj_t *ui_setrgbsw;
extern lv_obj_t *ui_setww;
extern lv_obj_t *ui_setwwt;
extern lv_obj_t *ui_setwwsw;
extern lv_obj_t *ui_setdev;
extern lv_obj_t *ui_setdevt;
extern lv_obj_t *ui_setfac;
extern lv_obj_t *ui_setfact;
extern lv_obj_t *ui_settextp;
extern lv_obj_t *ui_Set_title;
extern lv_obj_t *ui_scontrolp;

extern lv_obj_t *ui_Page_Slider;
extern lv_obj_t *ui_bvpb;
extern lv_obj_t *ui_bvbimg;
extern lv_obj_t *ui_bvbt;
extern lv_obj_t *ui_bvbv;
extern lv_obj_t *ui_bvs;
extern lv_obj_t *ui_bvtp;
extern lv_obj_t *ui_bvtitle;
extern lv_obj_t *ui_bvback;
extern lv_obj_t *ui_vp;
extern lv_obj_t *ui_vslider;
extern lv_obj_t *ui_bp;
extern lv_obj_t *ui_bslider;

extern lv_obj_t *ui_Page_About;
extern lv_obj_t *ui_AboutP;
extern lv_obj_t *ui_aboutdevname;
extern lv_obj_t *ui_devnamet1;
extern lv_obj_t *ui_devnamet2;
extern lv_obj_t *ui_aboutespversion;
extern lv_obj_t *ui_espversiont1;
extern lv_obj_t *ui_espversiont2;
extern lv_obj_t *ui_aboutaiversion;
extern lv_obj_t *ui_aiversion1;
extern lv_obj_t *ui_aiversion2;
extern lv_obj_t *ui_aboutsn;
extern lv_obj_t *ui_snt1;
extern lv_obj_t *ui_snt2;
extern lv_obj_t *ui_abouteui;
extern lv_obj_t *ui_euit1;
extern lv_obj_t *ui_euit2;
extern lv_obj_t *ui_aboutblemac;
extern lv_obj_t *ui_blet1;
extern lv_obj_t *ui_blet2;
extern lv_obj_t *ui_aboutwifimac;
extern lv_obj_t *ui_wifit1;
extern lv_obj_t *ui_wifit2;
extern lv_obj_t *ui_Paboutb;
extern lv_obj_t *ui_abtp;
extern lv_obj_t *ui_abtitle;

extern lv_obj_t *ui_Page_Swipe;
extern lv_obj_t *ui_spsilder;
extern lv_obj_t *ui_spback;
extern lv_obj_t *ui_sptext;
extern lv_obj_t *ui_swipep;
extern lv_obj_t *ui_sptitle;
extern lv_obj_t *ui_swipep2;
extern lv_obj_t *ui_sptext2;
extern lv_obj_t *ui_Spinner4;

extern lv_obj_t *ui_Page_Sleep;
extern lv_obj_t *ui_sleeptimeroller;
extern lv_obj_t *ui_slpback;
extern lv_obj_t *ui_sleepswitchp;
extern lv_obj_t *ui_sleepswitcht;
extern lv_obj_t *ui_sleepswitch;

extern lv_obj_t *ui_Page_Connect;
extern lv_obj_t *ui_conn_arcl;
extern lv_obj_t *ui_conn_arcr;
extern lv_obj_t *ui_conn_panel1;
extern lv_obj_t *ui_connp11;
extern lv_obj_t *ui_connp12;
extern lv_obj_t *ui_connp13;
extern lv_obj_t *ui_connp14;
extern lv_obj_t *ui_conn_panel2;
extern lv_obj_t *ui_connp21;
extern lv_obj_t *ui_connp22;
extern lv_obj_t *ui_connp23;
extern lv_obj_t *ui_connp24;
extern lv_obj_t *ui_conn_QR;
extern lv_obj_t *ui_conncancel;
extern lv_obj_t *ui_connp1;
extern lv_obj_t *ui_arrow1;
extern lv_obj_t *ui_connp2;
extern lv_obj_t *ui_arrow2;

extern lv_obj_t *ui_Page_Network;
extern lv_obj_t *ui_wifip1;
extern lv_obj_t *ui_wifiicon;
extern lv_obj_t *ui_wifissid;
extern lv_obj_t *ui_wifibtnt;
extern lv_obj_t *ui_wifimqtt;
extern lv_obj_t *ui_wifip3;
extern lv_obj_t *ui_wifitext3;
extern lv_obj_t *ui_wifilogo;
extern lv_obj_t *ui_wifitext2;
extern lv_obj_t *ui_wificancel;

void ui_event_Page_Set(lv_event_t *e);
void ui_event_setback(lv_event_t *e);
void ui_event_setdown(lv_event_t *e);
void ui_event_setapp(lv_event_t *e);
void ui_event_setwifi(lv_event_t *e);
void ui_event_setble(lv_event_t *e);
void ui_event_setvol(lv_event_t *e);
void ui_event_setbri(lv_event_t *e);
void ui_event_settime(lv_event_t *e);
void ui_event_setrgb(lv_event_t *e);
void ui_event_setww(lv_event_t *e);
void ui_event_setdev(lv_event_t *e);
void ui_event_setfac(lv_event_t *e);

void ui_event_bvback(lv_event_t *e);
void ui_event_vslider(lv_event_t *e);
void ui_event_bslider(lv_event_t *e);

void ui_event_aboutdevname(lv_event_t *e);
void ui_event_aboutespversion(lv_event_t *e);
void ui_event_aboutaiversion(lv_event_t *e);
void ui_event_aboutsn(lv_event_t *e);
void ui_event_abouteui(lv_event_t *e);
void ui_event_aboutblemac(lv_event_t *e);
void ui_event_aboutwifimac(lv_event_t *e);
void ui_event_Paboutb(lv_event_t *e);

void ui_event_spsilder(lv_event_t *e);
void ui_event_spback(lv_event_t *e);

void ui_event_sleeptimeroller(lv_event_t *e);
void ui_event_slpback(lv_event_t *e);
void ui_event_sleepswitchp(lv_event_t *e);
void ui_event_sleepswitch(lv_event_t *e);

void ui_event_conncancel(lv_event_t *e);
void ui_event_connp1(lv_event_t *e);
void ui_event_arrow1(lv_event_t *e);
void ui_event_connp2(lv_event_t *e);
void ui_event_arrow2(lv_event_t *e);
void ui_event_wificancel(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif
