
#include "e_gem.h"

typedef void (*WIN_REDRAW)(short,WIN*,GRECT*);
typedef void (*WIN_DRAW)(short,WIN*,GRECT*,GRECT*,void*);

/* CAST is used to initialize ob_spec fields in static OBJECT arrays.
   In modern gemlib, ob_spec is the OBSPEC union; use (long) to
   initialize the .index member (the first field). */
#define CAST	(long)

typedef struct
{
	short msg[8];
} MESSAG;

typedef struct
{
	short msg_max,msg_tail,msg_head;
	MESSAG *msg;
} XMSG;

/* E_GEM was written for 16-bit int; use a 16-bit unsigned type */
typedef unsigned short uint16_gem;

typedef struct
{
	uint16_gem mode;
	long index;
	uint16_gem dev,reserved1,nlink,uid,gid;
	long size,blksize,nblocks;
	short mtime,mdate;
	short atime,adate;
	short ctime,cdate;
	short attr,reserved2;
	long reserved3[2];
} X_ATTR;

#ifndef SMALL_NO_ICONIFY
#define win_iconified(win)	(win->iconified & (ICONIFIED|ICFS))
#endif

#define	FONT_WAIT	0
#define	XACC_WAIT	1
#define AV_WAIT		2
#define PAULA_WAIT	3

#ifndef SMALL_NO_MENU
extern	OBJECT *_menu;
#endif

extern	char *_win_id;
extern	IMAGES _radios,_checks,_arrows_down,_arrows_up,_arrows_left,_arrows_right,_cycles;
extern	boolean _back_win,_nonsel_fly,_dial_round,_app_mouse;
extern	short _up_test,_rc_handle,_untop,_mouse_off;
extern  short _opened,_ac_close,_alert_color,_min_timer,_no_output,_ibm_hot,_small_hot;
#ifndef SMALL_NO_ICONIFY
extern	short _iconified;
#endif
extern	WIN *_window_list[];

#define output	(_dia_len==0 && !_no_output)
#define protect	(mint || magx>=0x0300)

extern	short _theight;

extern  MITEM _menu_items[];
extern  short _mitems_cnt;

extern	MITEM *_xmenu_items;
extern	short _xitems_cnt;

#ifndef SMALL_NO_SCROLL
extern	short _scroll,_scroll_all;
#endif

#ifndef SMALL_NO_HZ
extern DIAINFO *_last_cursor;
extern short _crs_hz,_no_edit;
#endif
#ifndef SMALL_EDIT
extern short _ascii,_ascii_digit;
#ifndef SMALL_NO_CLIPBRD
extern short _edit_clip;
#endif
#endif

#ifndef SMALL_NO_XACC_AV
extern short _xacc_msgs;
#endif

#ifndef SMALL_NO_DD
extern  short _dd_available;
#endif

#define TOP_TIMER	500

extern long _top_timer;
extern long _Topper(long,long,MKSTATE *);

extern  DIAINFO	*_dia_list[MAX_DIALS+1];
extern	short	_dia_len,_win_len;

#ifndef SMALL_NO_ICONIFY
extern	short cdecl (*_icfs)(short,...);
short cdecl _default_icfs(short,...);
#endif

void	_init_dialog(XEVENT *);

boolean	_is_hidden(OBJECT *,short);
short		_is_hotkey(OBJECT *,short);
void	_check_hotkeys(OBJECT *);
short		_get_hotkey(OBJECT *,short);
short		_set_hotkey(OBJECT *,OBJECT *,char);

void	_vdi_attr(short,short,short,short);
short		_rc_sc_savetree(OBJECT *,RC_RECT *);
void 	_bar(short,short,short,short,short,short,short,short);

void	_new_top(short,short);
void	_get_font_size(OBJECT *,short *,short *,short *);

#ifndef SMALL_NO_EDIT
char	*_edit_get_info(OBJECT *,short,short,EDINFO *);
void	_calc_cursor(DIAINFO *,EDINFO *,GRECT *);
void	_cursor_off(DIAINFO *);
#ifndef SMALL_EDIT
void	_insert_history(DIAINFO *);
#endif
short		_insert_buf(DIAINFO *,char *,short);
void	_objc_edit_handler(DIAINFO *,short,short,XEVENT *,short *);
short		_next_edit(DIAINFO *,short);
#endif

short		_messag_handler(short,XEVENT *,short *,DIAINFO **);
short		_send_msg(void *,short,short,short,short);
void	_inform(short);
void	_inform_buffered(short);
void 	_send_puf(short,short,short *);

#ifndef SMALL_NO_SCROLL
void	_window_scroll_pos(WIN *,short);
void	_arrow_window(SCROLL *,short,short);
#endif
short 	_call_event_handler(short,XEVENT *,short);

#ifndef SMALL_NO_FSEL
short		_FselEvent(short,short *,XEVENT *);
#endif

#ifndef SMALL_NO_FONT
short			_InitFont(void);
#endif

#ifndef SMALL_NO_XACC_AV
void	_XAccSendStartup(char *,char *,short,short,short);
void	_XAccAvExit(void);
short		_XAccComm(short *);
void	_AvAllWins(void);
void	_MultiAv(void);
short		_Wait(short,short,short,char*);
#endif

#ifndef SMALL_NO_DD
void	_rec_ddmsg(short*);
#endif

#ifndef SMALL_NO_CLIPBRD
void	_scrp_init(void);
#endif

void	_clip_rect(GRECT *);
void	_ob_xdraw(OBJECT *,short,GRECT *);
