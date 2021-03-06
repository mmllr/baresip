/**
 * @file menu.c  Interactive menu
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <time.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup menu menu
 *
 * Interactive menu
 *
 * This module must be loaded if you want to use the interactive menu
 * to control the Baresip application.
 */


/** Defines the status modes */
enum statmode {
	STATMODE_CALL = 0,
	STATMODE_OFF,
};


static struct {
	struct tmr tmr_alert;         /**< Incoming call alert timer      */
	struct tmr tmr_stat;          /**< Call status timer              */
	struct play *play;            /**< Current audio player state     */
	struct mbuf *dialbuf;         /**< Buffer for dialled number      */
	struct le *le_cur;            /**< Current User-Agent (struct ua) */
	bool bell;                    /**< ANSI Bell alert enabled        */
	bool ringback_disabled;	      /**< no ringback on sip 180 respons */
	struct tmr tmr_redial;        /**< Timer for auto-reconnect       */
	uint32_t redial_delay;        /**< Redial delay in [seconds]      */
	uint32_t redial_attempts;     /**< Number of re-dial attempts     */
	uint32_t current_attempts;    /**< Current number of re-dials     */
	uint64_t start_ticks;         /**< Ticks when app started         */
	enum statmode statmode;       /**< Status mode                    */
	bool clean_number;            /**< Remove -/() from diald numbers */
	char redial_aor[128];
} menu;


static int  menu_set_incall(bool incall);
static void update_callstatus(bool incall);
static void alert_stop(void);
static int switch_audio_source(struct re_printf *pf, void *arg);
static int switch_audio_player(struct re_printf *pf, void *arg);
static int switch_video_source(struct re_printf *pf, void *arg);
static void alert_start(void *arg);


static void redial_reset(void)
{
	tmr_cancel(&menu.tmr_redial);
	menu.current_attempts = 0;
}


static const char *translate_errorcode(uint16_t scode)
{
	switch (scode) {

	case 404: return "notfound.wav";
	case 486: return "busy.wav";
	case 487: return NULL; /* ignore */
	default:  return "error.wav";
	}
}


static void menu_play(const char *fname, int repeat)
{
	struct config *cfg = conf_config();
	struct player *player = baresip_player();

	menu.play = mem_deref(menu.play);
	(void)play_file(&menu.play, player, fname, repeat,
			cfg->audio.play_mod,
			cfg->audio.play_dev);
}


static void play_incoming(const struct ua *ua, bool waiting)
{
	/* stop any ringtones */
	menu.play = mem_deref(menu.play);

	/* Only play the ringtones if answermode is "Manual".
	 * If the answermode is "auto" then be silent.
	 */
	if (ANSWERMODE_MANUAL == account_answermode(ua_account(ua))) {

		if (waiting) {
			menu_play("callwaiting.wav", 3);
		}
		else {
			/* Alert user */
			menu_play("ring.wav", -1);
		}

		if (menu.bell)
			alert_start(0);
	}
}


static void play_ringback(void)
{
	/* stop any ringtones */
	menu.play = mem_deref(menu.play);

	if (menu.ringback_disabled) {
		info("\nRingback disabled\n");
	}
	else {
		menu_play("ringback.wav", -1);
	}
}


static void play_resume(const struct call *call)
{
	struct le *lec;
	struct le *leu;
	struct ua *uain;
	bool incoming = false;
	bool ringing = false;

	for (leu = uag_list()->head; leu; leu = leu->next) {
		struct ua *ua = leu->data;

		for (lec = ua_calls(ua)->head; lec; lec = lec->next) {
			if (lec->data == call)
				continue;

			switch (call_state(lec->data)) {
			case CALL_STATE_EARLY:
			case CALL_STATE_ESTABLISHED:
				return;
			case CALL_STATE_INCOMING:
				incoming = true;
				uain = ua;
				break;
			case CALL_STATE_RINGING:
				ringing = true;
				break;
			default:
				break;
			}
		}
	}

	if (incoming) {
		play_incoming(uain, uag_call_count() > 2);
	}
	else if (ringing) {
		play_ringback();
	}
}


static bool has_established_call(void)
{
	struct le *lec;
	struct le *leu;

	for (leu = uag_list()->head; leu; leu = leu->next) {
		struct ua *ua = leu->data;

		for (lec = ua_calls(ua)->head; lec; lec = lec->next) {

			switch (call_state(lec->data)) {
			case CALL_STATE_EARLY:
			case CALL_STATE_ESTABLISHED:
				return true;
			default:
				break;
			}
		}
	}

	return false;
}


static void check_registrations(void)
{
	static bool ual_ready = false;
	struct le *le;
	uint32_t n;

	if (ual_ready)
		return;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;

		if (!ua_isregistered(ua) && !account_prio(ua_account(ua)))
			return;
	}

	n = list_count(uag_list());

	/* We are ready */
	ui_output(baresip_uis(),
		  "\x1b[32mAll %u useragent%s registered successfully!"
		  " (%u ms)\x1b[;m\n",
		  n, n==1 ? "" : "s",
		  (uint32_t)(tmr_jiffies() - menu.start_ticks));

	ual_ready = true;
}


/**
 * Print the SIP Registration for all User-Agents
 *
 * @param pf     Print handler for debug output
 * @param unused Unused parameter
 *
 * @return 0 if success, otherwise errorcode
 */
static int ua_print_reg_status(struct re_printf *pf, void *unused)
{
	struct le *le;
	int err;

	(void)unused;

	err = re_hprintf(pf, "\n--- User Agents (%u) ---\n",
			 list_count(uag_list()));

	for (le = list_head(uag_list()); le && !err; le = le->next) {
		const struct ua *ua = le->data;

		err  = re_hprintf(pf, "%s ", ua == uag_current() ? ">" : " ");
		err |= ua_print_status(pf, ua);
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


static int cmd_set_answermode(struct re_printf *pf, void *arg)
{
	enum answermode mode;
	const struct cmd_arg *carg = arg;
	int err;

	if (0 == str_cmp(carg->prm, "manual")) {
		mode = ANSWERMODE_MANUAL;
	}
	else if (0 == str_cmp(carg->prm, "early")) {
		mode = ANSWERMODE_EARLY;
	}
	else if (0 == str_cmp(carg->prm, "auto")) {
		mode = ANSWERMODE_AUTO;
	}
	else {
		(void)re_hprintf(pf, "Invalid answer mode: %s\n", carg->prm);
		return EINVAL;
	}

	err = account_set_answermode(ua_account(uag_current()), mode);
	if (err)
		return err;

	(void)re_hprintf(pf, "Answer mode changed to: %s\n", carg->prm);

	return 0;
}


/**
 * Print the current SIP Call status for the current User-Agent
 *
 * @param pf     Print handler for debug output
 * @param unused Unused parameter
 *
 * @return 0 if success, otherwise errorcode
 */
static int ua_print_call_status(struct re_printf *pf, void *unused)
{
	struct call *call;
	int err;

	(void)unused;

	call = ua_call(uag_current());
	if (call) {
		err  = re_hprintf(pf, "\n%H\n", call_debug, call);
	}
	else {
		err  = re_hprintf(pf, "\n(no active calls)\n");
	}

	return err;
}


static void clean_number(char *str)
{
	int i = 0, k = 0;

	/* only clean numeric numbers
	 * In other cases trust the user input
	 */
	int err = re_regex(str, sizeof(str), "[A-Za-z]");
	if (err == 0)
		return;

	/* remove (0) which is in some mal-formated numbers
	 * but only if trailed by another character
	 */
	if (str[0] == '+' || (str[0] == '0' && str[1] == '0'))
		while (str[i]) {
			if (str[i] == '('
			 && str[i+1] == '0'
			 && str[i+2] == ')'
			 && (str[i+3] == ' '
				 || (str[i+3] >= '0' && str[i+3] <= '9')
			    )
			) {
				str[i+1] = ' ';
				break;
			}
			++i;
		}
	i = 0;
	while (str[i]) {
		if (str[i] == ' '
		 || str[i] == '.'
		 || str[i] == '-'
		 || str[i] == '/'
		 || str[i] == '('
		 || str[i] == ')')
			++i;
		else
			str[k++] = str[i++];
	}
	str[k] = '\0';
}

static int dial_handler(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	if (str_isset(carg->prm)) {

		mbuf_rewind(menu.dialbuf);
		(void)mbuf_write_str(menu.dialbuf, carg->prm);
		if (menu.clean_number)
			clean_number(carg->prm);

		err = ua_connect(uag_current(), NULL, NULL,
				 carg->prm, VIDMODE_ON);
	}
	else if (menu.dialbuf->end > 0) {

		char *uri;

		menu.dialbuf->pos = 0;
		err = mbuf_strdup(menu.dialbuf, &uri, menu.dialbuf->end);
		if (err)
			return err;
		if (menu.clean_number)
			clean_number(uri);

		err = ua_connect(uag_current(), NULL, NULL, uri, VIDMODE_ON);

		mem_deref(uri);
	}

	if (err) {
		warning("menu: ua_connect failed: %m\n", err);
	}

	return err;
}


static void options_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	(void)arg;

	if (err) {
		warning("options reply error: %m\n", err);
		return;
	}

	if (msg->scode < 200)
		return;

	if (msg->scode < 300) {

		mbuf_set_pos(msg->mb, 0);
		info("----- OPTIONS of %r -----\n%b",
		     &msg->to.auri, mbuf_buf(msg->mb),
		     mbuf_get_left(msg->mb));
		return;
	}

	info("%r: OPTIONS failed: %u %r\n", &msg->to.auri,
	     msg->scode, &msg->reason);
}


static int options_command(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	err = ua_options_send(uag_current(), carg->prm,
			      options_resp_handler, NULL);
	if (err) {
		warning("menu: ua_options failed: %m\n", err);
	}

	return err;
}


static int cmd_answer(struct re_printf *pf, void *unused)
{
	struct ua *ua = uag_current();
	int err;
	(void)unused;

	err = re_hprintf(pf, "%s: Answering incoming call\n", ua_aor(ua));

	/* Stop any ongoing ring-tones */
	menu.play = mem_deref(menu.play);

	ua_hold_answer(ua, NULL, VIDMODE_ON);

	return err;
}


static int cmd_hangup(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;

	ua_hangup(uag_current(), NULL, 0, NULL);

	return 0;
}


static int create_ua(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = NULL;
	struct account *acc;
	int err = 0;

	if (str_isset(carg->prm)) {

		(void)re_hprintf(pf, "Creating UA for %s ...\n", carg->prm);
		err = ua_alloc(&ua, carg->prm);
		if (err)
			goto out;
	}

	acc = ua_account(ua);
	if (account_regint(acc)) {
		if (!account_prio(acc))
			(void)ua_register(ua);
		else
			(void)ua_fallback(ua);
	}

	err = ua_print_reg_status(pf, NULL);

 out:
	if (err) {
		(void)re_hprintf(pf, "menu: create_ua failed: %m\n", err);
	}

	return err;
}


static int cmd_ua_next(struct re_printf *pf, void *unused)
{
	int err;

	(void)pf;
	(void)unused;

	if (!menu.le_cur)
		menu.le_cur = list_head(uag_list());
	if (!menu.le_cur)
		return 0;

	menu.le_cur = menu.le_cur->next ?
		menu.le_cur->next : list_head(uag_list());

	err = re_hprintf(pf, "ua: %s\n", ua_aor(list_ledata(menu.le_cur)));

	uag_current_set(list_ledata(menu.le_cur));

	update_callstatus(uag_call_count());

	return err;
}


static int cmd_ua_find(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = NULL;

	if (str_isset(carg->prm)) {
		ua = uag_find_aor(carg->prm);
	}

	if (!ua) {
		warning("menu: ua_find failed: %s\n", carg->prm);
		return ENOENT;
	}

	re_hprintf(pf, "ua: %s\n", ua_aor(ua));

	uag_current_set(ua);

	update_callstatus(uag_call_count());

	return 0;
}


static int cmd_ua_delete(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = NULL;

	if (str_isset(carg->prm)) {
		ua = uag_find_aor(carg->prm);
	}

	if (!ua) {
		return ENOENT;
	}

	if (ua == uag_current()) {
		(void)cmd_ua_next(pf, NULL);
	}

	(void)re_hprintf(pf, "deleting ua: %s\n", carg->prm);
	mem_deref(ua);
	(void)ua_print_reg_status(pf, NULL);

	return 0;
}


#ifdef USE_TLS
static int cmd_tls_issuer(struct re_printf *pf, void *unused)
{
	int err = 0;
	struct mbuf *mb;
	(void)unused;

	mb = mbuf_alloc(20);
	if (!mb)
		return ENOMEM;

	err = tls_get_issuer(uag_tls(), mb);
	if (err){
		warning("menu: Unable to get certificate issuer (%m)\n", err);
		goto out;
	}

	(void)re_hprintf(pf, "TLS Cert Issuer: %b\n", mb->buf, mb->pos);

 out:
	mem_deref(mb);
	return err;
}


static int cmd_tls_subject(struct re_printf *pf, void *unused)
{
	int err = 0;
	struct mbuf *mb;
	(void)unused;

	mb = mbuf_alloc(20);
	if (!mb)
		return ENOMEM;

	err = tls_get_subject(uag_tls(), mb);
	if (err) {
		warning("menu: Unable to get certificate subject (%m)\n", err);
		goto out;
	}

	(void)re_hprintf(pf, "TLS Cert Subject: %b\n", mb->buf, mb->pos);

 out:
	mem_deref(mb);
	return err;
}
#endif


static int print_commands(struct re_printf *pf, void *unused)
{
	(void)unused;
	return cmd_print(pf, baresip_commands());
}


static int cmd_print_calls(struct re_printf *pf, void *unused)
{
	(void)unused;
	return ua_print_calls(pf, uag_current());
}


static const char about_fmt[] =
	".------------------------------------------------------------.\n"
	"|                      "
	"\x1b[34;1m" "bare"
	"\x1b[31;1m" "sip"
	"\x1b[;m"
	" %-10s                    |\n"
	"|                                                            |\n"
	"| Baresip is a portable and modular SIP User-Agent           |\n"
	"| with audio and video support                               |\n"
	"|                                                            |\n"
	"| License:   BSD                                             |\n"
	"| Homepage:  https://github.com/baresip/baresip              |\n"
	"|                                                            |\n"
	"'------------------------------------------------------------'\n"
	;


static int about_box(struct re_printf *pf, void *unused)
{
	(void)unused;

	return re_hprintf(pf, about_fmt, BARESIP_VERSION);
}


static const struct cmd cmdv[] = {

{"accept",    'a',        0, "Accept incoming call",    cmd_answer           },
{"hangup",    'b',        0, "Hangup call",             cmd_hangup           },
{"callstat",  'c',        0, "Call status",             ua_print_call_status },
{"dial",      'd',  CMD_PRM, "Dial",                    dial_handler         },
{"help",      'h',        0, "Help menu",               print_commands       },
{"listcalls", 'l',        0, "List active calls",       cmd_print_calls      },
{"options",   'o',  CMD_PRM, "Options",                 options_command      },
{"reginfo",   'r',        0, "Registration info",       ua_print_reg_status  },
{"answermode",0,    CMD_PRM, "Set answer mode",         cmd_set_answermode   },
{NULL,        KEYCODE_ESC,0, "Hangup call",             cmd_hangup           },
{"uanext",    'T',        0, "Toggle UAs",              cmd_ua_next          },
{"uanew",     0,    CMD_PRM, "Create User-Agent",       create_ua            },
{"uadel",     0,    CMD_PRM, "Delete User-Agent",       cmd_ua_delete        },
{"uafind",    0,    CMD_PRM, "Find User-Agent <aor>",   cmd_ua_find          },
#ifdef USE_TLS
{"tlsissuer", 0,          0, "TLS certificate issuer",  cmd_tls_issuer    },
{"tlssubject",0,          0, "TLS certificate subject", cmd_tls_subject   },
#endif
{"ausrc",     0,    CMD_PRM, "Switch audio source",     switch_audio_source  },
{"auplay",    0,    CMD_PRM, "Switch audio player",     switch_audio_player  },
{"about",     0,          0, "About box",               about_box            },
{"vidsrc",    0,    CMD_PRM, "Switch video source",     switch_video_source  },

};

static const struct cmd dialcmdv[] = {
/* Numeric keypad inputs: */
{NULL, '#', CMD_PRM, NULL,   dial_handler },
{NULL, '*', CMD_PRM, NULL,   dial_handler },
{NULL, '0', CMD_PRM, NULL,   dial_handler },
{NULL, '1', CMD_PRM, NULL,   dial_handler },
{NULL, '2', CMD_PRM, NULL,   dial_handler },
{NULL, '3', CMD_PRM, NULL,   dial_handler },
{NULL, '4', CMD_PRM, NULL,   dial_handler },
{NULL, '5', CMD_PRM, NULL,   dial_handler },
{NULL, '6', CMD_PRM, NULL,   dial_handler },
{NULL, '7', CMD_PRM, NULL,   dial_handler },
{NULL, '8', CMD_PRM, NULL,   dial_handler },
{NULL, '9', CMD_PRM, NULL,   dial_handler },
};


static int call_audio_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return audio_debug(pf, call_audio(ua_call(uag_current())));
}


static int call_reinvite(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;
	return call_modify(ua_call(uag_current()));
}


static int call_mute(struct re_printf *pf, void *unused)
{
	struct audio *audio = call_audio(ua_call(uag_current()));
	bool muted = !audio_ismuted(audio);
	(void)unused;

	(void)re_hprintf(pf, "\ncall %smuted\n", muted ? "" : "un-");
	audio_mute(audio, muted);

	return 0;
}


static int call_xfer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	(void)pf;

	return call_transfer(ua_call(uag_current()), carg->prm);
}


static int cmd_call_hold(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	return call_hold(ua_call(uag_current()), true);
}


static int cmd_call_resume(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	return call_hold(ua_call(uag_current()), false);
}


static int hold_prev_call(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	(void)pf;

	return call_hold(ua_prev_call(uag_current()), 'H' == carg->key);
}


static int switch_audio_player(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_audio *aucfg;
	struct config *cfg;
	struct audio *a;
	const struct auplay *ap;
	struct le *le;
	char driver[16], device[128] = "";
	int err = 0;

	if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
		     &pl_driver, &pl_device)) {

		return re_hprintf(pf, "\rFormat should be:"
				  " driver,device\n");
	}

	pl_strcpy(&pl_driver, driver, sizeof(driver));
	pl_strcpy(&pl_device, device, sizeof(device));

	ap = auplay_find(baresip_auplayl(), driver);
	if (!ap) {
		re_hprintf(pf, "no such audio-player: %s\n", driver);
		return 0;
	}
	else if (!list_isempty(&ap->dev_list)) {

		if (!mediadev_find(&ap->dev_list, device)) {
			re_hprintf(pf,
				   "no such device for %s audio-player: %s\n",
				   driver, device);

			mediadev_print(pf, &ap->dev_list);

			return 0;
		}
	}

	re_hprintf(pf, "switch audio player: %s,%s\n",
		   driver, device);

	cfg = conf_config();
	if (!cfg) {
		return re_hprintf(pf, "no config object\n");
	}

	aucfg = &cfg->audio;

	str_ncpy(aucfg->play_mod, driver, sizeof(aucfg->play_mod));
	str_ncpy(aucfg->play_dev, device, sizeof(aucfg->play_dev));

	str_ncpy(aucfg->alert_mod, driver, sizeof(aucfg->alert_mod));
	str_ncpy(aucfg->alert_dev, device, sizeof(aucfg->alert_dev));

	for (le = list_tail(ua_calls(uag_current())); le; le = le->prev) {

		struct call *call = le->data;

		a = call_audio(call);

		err = audio_set_player(a, driver, device);
		if (err) {
			re_hprintf(pf, "failed to set audio-player"
				   " (%m)\n", err);
			break;
		}
	}

	return 0;
}


static int switch_audio_source(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_audio *aucfg;
	struct config *cfg;
	struct audio *a;
	const struct ausrc *as;
	struct le *le;
	char driver[16], device[128] = "";
	int err = 0;

	if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
		     &pl_driver, &pl_device)) {

		return re_hprintf(pf, "\rFormat should be:"
				  " driver,device\n");
	}

	pl_strcpy(&pl_driver, driver, sizeof(driver));
	pl_strcpy(&pl_device, device, sizeof(device));

	as = ausrc_find(baresip_ausrcl(), driver);
	if (!as) {
		re_hprintf(pf, "no such audio-source: %s\n", driver);
		return 0;
	}
	else if (!list_isempty(&as->dev_list)) {

		if (!mediadev_find(&as->dev_list, device)) {
			re_hprintf(pf,
				   "no such device for %s audio-source: %s\n",
				   driver, device);

			mediadev_print(pf, &as->dev_list);

			return 0;
		}
	}

	re_hprintf(pf, "switch audio device: %s,%s\n",
		   driver, device);

	cfg = conf_config();
	if (!cfg) {
		return re_hprintf(pf, "no config object\n");
	}

	aucfg = &cfg->audio;

	str_ncpy(aucfg->src_mod, driver, sizeof(aucfg->src_mod));
	str_ncpy(aucfg->src_dev, device, sizeof(aucfg->src_dev));

	for (le = list_tail(ua_calls(uag_current())); le; le = le->prev) {

		struct call *call = le->data;

		a = call_audio(call);

		err = audio_set_source(a, driver, device);
		if (err) {
			re_hprintf(pf, "failed to set audio-source"
				   " (%m)\n", err);
			break;
		}
	}

	return 0;
}


static int switch_video_source(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_video *vidcfg;
	struct config *cfg;
	struct video *v;
	const struct vidsrc *vs;
	struct le *le;
	char driver[16], device[128] = "";
	int err = 0;

	if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
		     &pl_driver, &pl_device)) {

		return re_hprintf(pf, "\rFormat should be:"
				  " driver,device\n");
	}

	pl_strcpy(&pl_driver, driver, sizeof(driver));
	pl_strcpy(&pl_device, device, sizeof(device));

	vs = vidsrc_find(baresip_vidsrcl(), driver);
	if (!vs) {
		re_hprintf(pf, "no such video-source: %s\n", driver);
		return 0;
	}
	else if (!list_isempty(&vs->dev_list)) {

		if (!mediadev_find(&vs->dev_list, device)) {
			re_hprintf(pf,
				   "no such device for %s video-source: %s\n",
				   driver, device);

			mediadev_print(pf, &vs->dev_list);

			return 0;
		}
	}

	re_hprintf(pf, "switch video device: %s,%s\n",
		   driver, device);

	cfg = conf_config();
	if (!cfg) {
		return re_hprintf(pf, "no config object\n");
	}

	vidcfg = &cfg->video;

	str_ncpy(vidcfg->src_mod, driver, sizeof(vidcfg->src_mod));
	str_ncpy(vidcfg->src_dev, device, sizeof(vidcfg->src_dev));

	for (le = list_tail(ua_calls(uag_current())); le; le = le->prev) {

		struct call *call = le->data;

		v = call_video(call);

		err = video_set_source(v, driver, device);
		if (err) {
			re_hprintf(pf, "failed to set video-source"
				   " (%m)\n", err);
			break;
		}
	}

	return 0;
}


static int call_video_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return video_debug(pf, call_video(ua_call(uag_current())));
}


static int set_video_dir(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_INACTIVE))) {
		err = call_set_video_dir(ua_call(uag_current()), SDP_INACTIVE);
	}
	else if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_SENDONLY))) {
		err = call_set_video_dir(ua_call(uag_current()), SDP_SENDONLY);
	}
	else if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_RECVONLY))) {
		err = call_set_video_dir(ua_call(uag_current()), SDP_RECVONLY);
	}
	else if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_SENDRECV))) {
		err = call_set_video_dir(ua_call(uag_current()), SDP_SENDRECV);
	}
	else {
		(void)re_hprintf(pf, "Invalid video direction %s"
			" (inactive, sendonly, recvonly, sendrecv)\n",
			carg->prm);
		return EINVAL;
	}

	return err;
}


static int digit_handler(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct call *call;
	int err = 0;

	(void)pf;

	call = ua_call(uag_current());
	if (call)
		err = call_send_digit(call, carg->key);

	return err;
}


static int send_code(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct call *call;
	size_t i;
	int err = 0;
	(void)pf;

	call = ua_call(uag_current());
	if (call) {
		for (i = 0; i < str_len(carg->prm) && !err; i++) {
			err = call_send_digit(call, carg->prm[i]);
		}
		if (!err) {
			err = call_send_digit(call, KEYCODE_REL);
		}
	}

	return err;
}


static int toggle_statmode(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	if (menu.statmode == STATMODE_OFF)
		menu.statmode = STATMODE_CALL;
	else
		menu.statmode = STATMODE_OFF;

	return 0;
}


static int set_current_call(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct call *call;
	uint32_t linenum = atoi(carg->prm);
	int err;

	call = call_find_linenum(ua_calls(uag_current()), linenum);
	if (call) {
		err = re_hprintf(pf, "setting current call: line %u\n",
				 linenum);
		call_set_current(ua_calls(uag_current()), call);
	}
	else {
		err = re_hprintf(pf, "call not found\n");
	}

	return err;
}


static int set_audio_bitrate(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct call *call;
	uint32_t bitrate = str_isset(carg->prm) ? atoi(carg->prm) : 0;
	int err;

	call = ua_call(uag_current());
	if (call) {
		err = re_hprintf(pf, "setting audio bitrate: %u bps\n",
				 bitrate);
		audio_set_bitrate(call_audio(call), bitrate);
	}
	else {
		err = re_hprintf(pf, "call not found\n");
	}

	return err;
}


static int cmd_find_call(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	const char *id = carg->prm;
	struct list *calls = ua_calls(uag_current());
	struct call *call;
	int err;

	call = call_find_id(calls, id);
	if (call) {
		err = re_hprintf(pf, "setting current call: %s\n", id);
		call_set_current(calls, call);
	}
	else {
		err = re_hprintf(pf, "call not found (id=%s)\n", id);
	}

	return err;
}


static const struct cmd callcmdv[] = {

{"aubitrate",    0,  CMD_PRM, "Set audio bitrate",    set_audio_bitrate    },
{"audio_debug", 'A',       0, "Audio stream",         call_audio_debug     },
{"callfind",     0,  CMD_PRM, "Find call <callid>",   cmd_find_call        },
{"hold",        'x',       0, "Call hold",            cmd_call_hold        },
{"line",        '@', CMD_PRM, "Set current call <line>", set_current_call  },
{"mute",        'm',       0, "Call mute/un-mute",    call_mute            },
{"prevhold",    'H',       0, "Hold previous call",   hold_prev_call       },
{"prevresume",  'L',       0, "Resume previous call", hold_prev_call       },
{"reinvite",    'I',       0, "Send re-INVITE",       call_reinvite        },
{"resume",      'X',       0, "Call resume",          cmd_call_resume      },
{"sndcode",      0,  CMD_PRM, "Send Code",            send_code            },
{"statmode",    'S',       0, "Statusmode toggle",    toggle_statmode      },
{"transfer",    't', CMD_PRM, "Transfer call",        call_xfer            },
{"video_debug", 'V',       0, "Video stream",         call_video_debug     },
{"video_dir",     0, CMD_PRM, "Set video direction",  set_video_dir        },

/* Numeric keypad for DTMF events: */
{NULL, '#',         0, NULL,                  digit_handler         },
{NULL, '*',         0, NULL,                  digit_handler         },
{NULL, '0',         0, NULL,                  digit_handler         },
{NULL, '1',         0, NULL,                  digit_handler         },
{NULL, '2',         0, NULL,                  digit_handler         },
{NULL, '3',         0, NULL,                  digit_handler         },
{NULL, '4',         0, NULL,                  digit_handler         },
{NULL, '5',         0, NULL,                  digit_handler         },
{NULL, '6',         0, NULL,                  digit_handler         },
{NULL, '7',         0, NULL,                  digit_handler         },
{NULL, '8',         0, NULL,                  digit_handler         },
{NULL, '9',         0, NULL,                  digit_handler         },
{NULL, KEYCODE_REL, 0, NULL,                  digit_handler         },

};


static int menu_set_incall(bool incall)
{
	struct commands *commands = baresip_commands();
	int err = 0;

	/* Dynamic menus */
	if (incall) {
		cmd_unregister(commands, dialcmdv);

		if (!cmds_find(commands, callcmdv)) {
			err = cmd_register(commands,
					   callcmdv, ARRAY_SIZE(callcmdv));
		}
	}
	else {
		cmd_unregister(commands, callcmdv);

		if (!cmds_find(commands, dialcmdv)) {
			err = cmd_register(baresip_commands(), dialcmdv,
					   ARRAY_SIZE(dialcmdv));
		}
	}
	if (err) {
		warning("menu: set_incall: cmd_register failed (%m)\n", err);
	}

	return err;
}


static void tmrstat_handler(void *arg)
{
	struct call *call;
	(void)arg;

	/* the UI will only show the current active call */
	call = ua_call(uag_current());
	if (!call)
		return;

	tmr_start(&menu.tmr_stat, 100, tmrstat_handler, 0);

	if (ui_isediting(baresip_uis()))
		return;

	if (STATMODE_OFF != menu.statmode) {
		(void)re_fprintf(stderr, "%H\r", call_status, call);
	}
}


static void update_callstatus(bool incall)
{
	/* if there are any active calls, enable the call status view */
	if (incall)
		tmr_start(&menu.tmr_stat, 100, tmrstat_handler, 0);
	else
		tmr_cancel(&menu.tmr_stat);
}


static void alert_start(void *arg)
{
	(void)arg;

	if (!menu.bell)
		return;

	ui_output(baresip_uis(), "\033[10;1000]\033[11;1000]\a");

	tmr_start(&menu.tmr_alert, 1000, alert_start, NULL);
}


static void alert_stop(void)
{
	if (!menu.bell)
		return;

	if (tmr_isrunning(&menu.tmr_alert))
		ui_output(baresip_uis(), "\r");

	tmr_cancel(&menu.tmr_alert);
}


static void redial_handler(void *arg)
{
	char *uri = NULL;
	int err;
	(void)arg;

	info("now: redialing now. current_attempts=%u, max_attempts=%u\n",
	     menu.current_attempts,
	     menu.redial_attempts);

	if (menu.current_attempts > menu.redial_attempts) {

		info("menu: redial: too many attemptes -- giving up\n");
		return;
	}

	if (menu.dialbuf->end == 0) {
		warning("menu: redial: dialbuf is empty\n");
		return;
	}

	menu.dialbuf->pos = 0;
	err = mbuf_strdup(menu.dialbuf, &uri, menu.dialbuf->end);
	if (err)
		return;

	err = ua_connect(uag_find_aor(menu.redial_aor), NULL, NULL,
			 uri, VIDMODE_ON);
	if (err) {
		warning("menu: redial: ua_connect failed (%m)\n", err);
	}

	mem_deref(uri);
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct call *call2 = NULL;
	bool incall;
	int err;
	(void)prm;
	(void)arg;

#if 0
	debug("menu: [ ua=%s call=%s ] event: %s (%s)\n",
	      ua_aor(ua), call_id(call), uag_event_str(ev), prm);
#endif


	switch (ev) {

	case UA_EVENT_CALL_INCOMING:

		/* set the current User-Agent to the one with the call */
		uag_current_set(ua);

		info("%s: Incoming call from: %s %s -"
		     " (press 'a' to accept)\n",
		     ua_aor(ua), call_peername(call), call_peeruri(call));

		play_incoming(ua, uag_call_count() > 1);
		break;

	case UA_EVENT_CALL_RINGING:
		if (call == ua_call(uag_current()) && !has_established_call())
			play_ringback();
		break;

	case UA_EVENT_CALL_PROGRESS:
		if (call == ua_call(uag_current()))
			menu.play = mem_deref(menu.play);
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		/* stop any ringtones */
		menu.play = mem_deref(menu.play);

		alert_stop();

		/* We must stop the re-dialing if the call was
		   established */
		redial_reset();
		break;

	case UA_EVENT_CALL_CLOSED:
		/* stop any ringtones */
		menu.play = mem_deref(menu.play);

		if (call_scode(call)) {
			const char *tone;
			tone = translate_errorcode(call_scode(call));
			if (tone)
				menu_play(tone, 1);
		}

		alert_stop();
		play_resume(call);

		/* Activate the re-dialing if:
		 *
		 * - redial_attempts must be enabled in config
		 * - the closed call must be of outgoing direction
		 * - the closed call must fail with special code 701
		 */
		if (menu.redial_attempts) {

			if (menu.current_attempts
			    ||
			    (call_is_outgoing(call) &&
			     call_scode(call) == 701)) {

				info("menu: call closed"
				     " -- redialing in %u seconds\n",
				     menu.redial_delay);

				++menu.current_attempts;

				str_ncpy(menu.redial_aor, ua_aor(ua),
					 sizeof(menu.redial_aor));

				tmr_start(&menu.tmr_redial,
					  menu.redial_delay*1000,
					  redial_handler, NULL);
			}
			else {
				info("menu: call closed -- not redialing\n");
			}
		}
		break;

	case UA_EVENT_CALL_TRANSFER:
		/*
		 * Create a new call to transfer target.
		 *
		 * NOTE: we will automatically connect a new call to the
		 *       transfer target
		 */

		info("menu: transferring call %s to '%s'\n",
		     call_id(call), prm);

		err = ua_call_alloc(&call2, ua, VIDMODE_ON, NULL, call,
				    call_localuri(call), true);
		if (!err) {
			struct pl pl;

			pl_set_str(&pl, prm);

			err = call_connect(call2, &pl);
			if (err) {
				warning("ua: transfer: connect error: %m\n",
					err);
			}
		}

		if (err) {
			(void)call_notify_sipfrag(call, 500, "Call Error");
			mem_deref(call2);
		}
		break;

	case UA_EVENT_CALL_TRANSFER_FAILED:
		info("menu: transfer failure: %s\n", prm);
		break;

	case UA_EVENT_REGISTER_OK:
		check_registrations();
		break;

	case UA_EVENT_UNREGISTERING:
		return;

	case UA_EVENT_MWI_NOTIFY:
		info("----- MWI for %s -----\n", ua_aor(ua));
		info("%s\n", prm);
		break;

	case UA_EVENT_AUDIO_ERROR:
		info("menu: audio error (%s)\n", prm);
		break;

	default:
		break;
	}

	incall = ev == UA_EVENT_CALL_CLOSED ?
			uag_call_count() > 1 : uag_call_count();
	menu_set_incall(incall);
	update_callstatus(incall);
}


static void message_handler(struct ua *ua, const struct pl *peer,
			    const struct pl *ctype,
			    struct mbuf *body, void *arg)
{
	struct config *cfg;
	(void)ua;
	(void)ctype;
	(void)arg;

	cfg = conf_config();

	ui_output(baresip_uis(), "\r%r: \"%b\"\n",
		  peer, mbuf_buf(body), mbuf_get_left(body));

	(void)play_file(NULL, baresip_player(), "message.wav", 0,
	                cfg->audio.alert_mod, cfg->audio.alert_dev);
}


static int module_init(void)
{
	struct pl val;
	int err;

	menu.bell = true;
	menu.redial_attempts = 0;
	menu.redial_delay = 5;
	menu.ringback_disabled = false;
	menu.statmode = STATMODE_CALL;
	menu.clean_number = false;

	/*
	 * Read the config values
	 */
	conf_get_bool(conf_cur(), "menu_bell", &menu.bell);
	conf_get_bool(conf_cur(), "ringback_disabled",
		      &menu.ringback_disabled);
	conf_get_bool(conf_cur(), "menu_clean_number", &menu.clean_number);

	if (0 == conf_get(conf_cur(), "redial_attempts", &val) &&
	    0 == pl_strcasecmp(&val, "inf")) {
		menu.redial_attempts = (uint32_t)-1;
	}
	else {
		conf_get_u32(conf_cur(), "redial_attempts",
			     &menu.redial_attempts);
	}
	conf_get_u32(conf_cur(), "redial_delay", &menu.redial_delay);

	if (menu.redial_attempts) {
		info("menu: redial enabled with %u attempts and"
		     " %u seconds delay\n",
		     menu.redial_attempts,
		     menu.redial_delay);
	}

	menu.dialbuf = mbuf_alloc(64);
	if (!menu.dialbuf)
		return ENOMEM;

	menu.start_ticks = tmr_jiffies();
	tmr_init(&menu.tmr_alert);

	if (0 == conf_get(conf_cur(), "statmode_default", &val) &&
	    0 == pl_strcasecmp(&val, "off")) {
		menu.statmode = STATMODE_OFF;
	}
	else {
		menu.statmode = STATMODE_CALL;
	}

	err  = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	err |= cmd_register(baresip_commands(), dialcmdv,
			    ARRAY_SIZE(dialcmdv));
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	err = message_listen(baresip_message(),
			     message_handler, NULL);
	if (err)
		return err;

	return err;
}


static int module_close(void)
{
	debug("menu: close (redial current_attempts=%d)\n",
	      menu.current_attempts);

	message_unlisten(baresip_message(), message_handler);

	uag_event_unregister(ua_event_handler);
	cmd_unregister(baresip_commands(), cmdv);
	cmd_unregister(baresip_commands(), dialcmdv);
	cmd_unregister(baresip_commands(), callcmdv);

	tmr_cancel(&menu.tmr_alert);
	tmr_cancel(&menu.tmr_stat);
	menu.dialbuf = mem_deref(menu.dialbuf);

	menu.le_cur = NULL;

	menu.play = mem_deref(menu.play);

	tmr_cancel(&menu.tmr_redial);

	return 0;
}


const struct mod_export DECL_EXPORTS(menu) = {
	"menu",
	"application",
	module_init,
	module_close
};
