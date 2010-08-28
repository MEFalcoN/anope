/* MemoServ functions.
 *
 * (C) 2003-2010 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

#include "services.h"
#include "modules.h"
#include "language.h"

/*************************************************************************/
/* *INDENT-OFF* */

static bool SendMemoMail(NickCore *nc, Memo *m);

/*************************************************************************/

void moduleAddMemoServCmds()
{
	ModuleManager::LoadModuleList(Config->MemoServCoreModules);
}

/*************************************************************************/
/*************************************************************************/
/* *INDENT-ON* */

/**
 * MemoServ initialization.
 * @return void
 */
void ms_init()
{
	moduleAddMemoServCmds();
}

/*************************************************************************/
/**
 * check_memos:  See if the given user has any unread memos, and send a
 *               NOTICE to that user if so (and if the appropriate flag is
 *               set).
 * @param u User Struct
 * @return void
 */
void check_memos(User *u)
{
	if (!u)
	{
		Log() << "check_memos called with NULL values";
		return;
	}

	const NickCore *nc = u->Account();
	if (!nc || !u->IsRecognized() || !nc->HasFlag(NI_MEMO_SIGNON))
		return;

	unsigned i = 0, end = nc->memos.memos.size(), newcnt = 0;
	for (; i < end; ++i)
	{
		if (nc->memos.memos[i]->HasFlag(MF_UNREAD))
			++newcnt;
	}
	if (newcnt > 0)
	{
		notice_lang(Config->s_MemoServ, u, newcnt == 1 ? MEMO_HAVE_NEW_MEMO : MEMO_HAVE_NEW_MEMOS, newcnt);
		if (newcnt == 1 && (nc->memos.memos[i - 1]->HasFlag(MF_UNREAD)))
			notice_lang(Config->s_MemoServ, u, MEMO_TYPE_READ_LAST, Config->s_MemoServ.c_str());
		else if (newcnt == 1)
		{
			for (i = 0; i < end; ++i)
			{
				if (nc->memos.memos[i]->HasFlag(MF_UNREAD))
					break;
			}
			notice_lang(Config->s_MemoServ, u, MEMO_TYPE_READ_NUM, Config->s_MemoServ.c_str(), nc->memos.memos[i]->number);
		}
		else
			notice_lang(Config->s_MemoServ, u, MEMO_TYPE_LIST_NEW, Config->s_MemoServ.c_str());
	}
	if (nc->memos.memomax > 0 && nc->memos.memos.size() >= nc->memos.memomax)
	{
		if (nc->memos.memos.size() > nc->memos.memomax)
			notice_lang(Config->s_MemoServ, u, MEMO_OVER_LIMIT, nc->memos.memomax);
		else
			notice_lang(Config->s_MemoServ, u, MEMO_AT_LIMIT, nc->memos.memomax);
	}
}

/*************************************************************************/
/*********************** MemoServ private routines ***********************/
/*************************************************************************/

/**
 * Return the MemoInfo corresponding to the given nick or channel name.
 * @param name Name to check
 * @param ischan - the result its a channel will be stored in here
 * @param isforbid - the result if its forbidden will be stored in here
 * @return `ischan' 1 if the name was a channel name, else 0.
 * @return `isforbid' 1 if the name is forbidden, else 0.
 */
MemoInfo *getmemoinfo(const Anope::string &name, bool &ischan, bool &isforbid)
{
	if (name[0] == '#')
	{
		ischan = true;
		ChannelInfo *ci = cs_findchan(name);
		if (ci)
		{
			if (!ci->HasFlag(CI_FORBIDDEN))
			{
				isforbid = false;
				return &ci->memos;
			}
			else
			{
				isforbid = true;
				return NULL;
			}
		}
		else
		{
			isforbid = false;
			return NULL;
		}
	}
	else
	{
		ischan = false;
		NickAlias *na = findnick(name);
		if (na)
		{
			if (!na->HasFlag(NS_FORBIDDEN))
			{
				isforbid = false;
				return &na->nc->memos;
			}
			else
			{
				isforbid = true;
				return NULL;
			}
		}
		else
		{
			isforbid = false;
			return NULL;
		}
	}
}

/*************************************************************************/

/**
 * Split from do_send, this way we can easily send a memo from any point
 * @param u User Struct
 * @param name Target of the memo
 * @param text Memo Text
 * @param z type see info
 *    0 - reply to user
 *    1 - silent
 *    2 - silent with no delay timer
 *    3 - reply to user and request read receipt
 * @return void
 */
void memo_send(User *u, const Anope::string &name, const Anope::string &text, int z)
{
	bool ischan, isforbid;
	MemoInfo *mi;
	time_t now = time(NULL);
	Anope::string source = u->Account()->display;
	int is_servoper = u->Account() && u->Account()->IsServicesOper();

	if (readonly)
		notice_lang(Config->s_MemoServ, u, MEMO_SEND_DISABLED);
	else if (text.empty())
	{
		if (!z)
			syntax_error(Config->s_MemoServ, u, "SEND", MEMO_SEND_SYNTAX);

		if (z == 3)
			syntax_error(Config->s_MemoServ, u, "RSEND", MEMO_RSEND_SYNTAX);
	}
	else if (!u->IsIdentified() && !u->IsRecognized())
	{
		if (!z || z == 3)
			notice_lang(Config->s_MemoServ, u, NICK_IDENTIFY_REQUIRED, Config->s_NickServ.c_str());
	}
	else if (!(mi = getmemoinfo(name, ischan, isforbid)))
	{
		if (!z || z == 3)
		{
			if (isforbid)
				notice_lang(Config->s_MemoServ, u, ischan ? CHAN_X_FORBIDDEN : NICK_X_FORBIDDEN, name.c_str());
			else
				notice_lang(Config->s_MemoServ, u, ischan ? CHAN_X_NOT_REGISTERED : NICK_X_NOT_REGISTERED, name.c_str());
		}
	}
	else if (z != 2 && Config->MSSendDelay > 0 && u && u->lastmemosend + Config->MSSendDelay > now)
	{
		u->lastmemosend = now;
		if (!z)
			notice_lang(Config->s_MemoServ, u, MEMO_SEND_PLEASE_WAIT, Config->MSSendDelay);

		if (z == 3)
			notice_lang(Config->s_MemoServ, u, MEMO_RSEND_PLEASE_WAIT, Config->MSSendDelay);
	}
	else if (!mi->memomax && !is_servoper)
	{
		if (!z || z == 3)
			notice_lang(Config->s_MemoServ, u, MEMO_X_GETS_NO_MEMOS, name.c_str());
	}
	else if (mi->memomax > 0 && mi->memos.size() >= mi->memomax && !is_servoper)
	{
		if (!z || z == 3)
			notice_lang(Config->s_MemoServ, u, MEMO_X_HAS_TOO_MANY_MEMOS, name.c_str());
	}
	else
	{
		u->lastmemosend = now;
		Memo *m = new Memo();
		mi->memos.push_back(m);
		m->sender = source;
		if (mi->memos.size() > 1)
		{
			m->number = mi->memos[mi->memos.size() - 2]->number + 1;
			if (m->number < 1)
			{
				for (unsigned i = 0, end = mi->memos.size(); i < end; ++i)
					mi->memos[i]->number = i + 1;
			}
		}
		else
			m->number = 1;
		m->time = time(NULL);
		m->text = text;
		m->SetFlag(MF_UNREAD);
		/* Set notify sent flag - DrStein */
		if (z == 2)
			m->SetFlag(MF_NOTIFYS);
		/* Set receipt request flag */
		if (z == 3)
			m->SetFlag(MF_RECEIPT);
		if (!z || z == 3)
			notice_lang(Config->s_MemoServ, u, MEMO_SENT, name.c_str());
		if (!ischan)
		{
			NickCore *nc = findnick(name)->nc;

			FOREACH_MOD(I_OnMemoSend, OnMemoSend(u, nc, m));

			if (Config->MSNotifyAll)
			{
				if (nc->HasFlag(NI_MEMO_RECEIVE) && !get_ignore(name))
				{
					for (std::list<NickAlias *>::iterator it = nc->aliases.begin(), it_end = nc->aliases.end(); it != it_end; ++it)
					{
						NickAlias *na = *it;
						User *user = finduser(na->nick);
						if (user && user->IsIdentified())
							notice_lang(Config->s_MemoServ, user, MEMO_NEW_MEMO_ARRIVED, source.c_str(), Config->s_MemoServ.c_str(), m->number);
					}
				}
				else
				{
					if ((u = finduser(name)) && u->IsIdentified() && nc->HasFlag(NI_MEMO_RECEIVE))
						notice_lang(Config->s_MemoServ, u, MEMO_NEW_MEMO_ARRIVED, source.c_str(), Config->s_MemoServ.c_str(), m->number);
				} /* if (flags & MEMO_RECEIVE) */
			}
			/* if (MSNotifyAll) */
			/* let's get out the mail if set in the nickcore - certus */
			if (nc->HasFlag(NI_MEMO_MAIL))
				SendMemoMail(nc, m);
		}
		else
		{
			Channel *c;

			FOREACH_MOD(I_OnMemoSend, OnMemoSend(u, cs_findchan(name), m));

			if (Config->MSNotifyAll && (c = findchan(name)))
			{
				for (CUserList::iterator it = c->users.begin(), it_end = c->users.end(); it != it_end; ++it)
				{
					UserContainer *cu = *it;

					if (check_access(cu->user, c->ci, CA_MEMO))
					{
						if (cu->user->Account() && cu->user->Account()->HasFlag(NI_MEMO_RECEIVE) && !get_ignore(cu->user->nick))
							notice_lang(Config->s_MemoServ, cu->user, MEMO_NEW_X_MEMO_ARRIVED, c->ci->name.c_str(), Config->s_MemoServ.c_str(), c->ci->name.c_str(), m->number);
					}
				}
			} /* MSNotifyAll */
		} /* if (!ischan) */
	} /* if command is valid */
}

/*************************************************************************/
/**
 * Delete a memo by number.
 * @param mi Memoinfo struct
 * @param num Memo number to delete
 * @return int 1 if the memo was found, else 0.
 */
bool delmemo(MemoInfo *mi, int num)
{
	if (mi->memos.empty())
		return false;

	unsigned i = 0, end = mi->memos.size();
	for (; i < end; ++i)
		if (mi->memos[i]->number == num)
			break;
	if (i < end)
	{
		delete mi->memos[i]; /* Deallocate the memo itself */
		mi->memos.erase(mi->memos.begin() + i); /* Remove the memo pointer from the vector */
		return true;
	}
	else
		return false;
}

/*************************************************************************/

static bool SendMemoMail(NickCore *nc, Memo *m)
{
	char message[BUFSIZE];

	snprintf(message, sizeof(message), getstring(NICK_MAIL_TEXT), nc->display.c_str(), m->sender.c_str(), m->number, m->text.c_str());

	return Mail(nc, getstring(MEMO_MAIL_SUBJECT), message);
}

/*************************************************************************/

/* Send receipt notification to sender. */

void rsend_notify(User *u, Memo *m, const Anope::string &chan)
{
	/* Only send receipt if memos are allowed */
	if (!readonly)
	{
		/* Get nick alias for sender */
		NickAlias *na = findnick(m->sender);

		if (!na)
			return;

		/* Get nick core for sender */
		NickCore *nc = na->nc;

		if (!nc)
			return;

		/* Text of the memo varies if the recepient was a
		   nick or channel */
		const char *fmt;
		char text[256];
		if (!chan.empty())
		{
			fmt = getstring(na, MEMO_RSEND_CHAN_MEMO_TEXT);
			snprintf(text, sizeof(text), fmt, chan.c_str());
		}
		else
		{
			fmt = getstring(na, MEMO_RSEND_NICK_MEMO_TEXT);
			snprintf(text, sizeof(text), "%s", fmt);
		}

		/* Send notification */
		memo_send(u, m->sender, text, 2);

		/* Notify recepient of the memo that a notification has
		   been sent to the sender */
		notice_lang(Config->s_MemoServ, u, MEMO_RSEND_USER_NOTIFICATION, nc->display.c_str());
	}

	/* Remove receipt flag from the original memo */
	m->UnsetFlag(MF_RECEIPT);

	return;
}
