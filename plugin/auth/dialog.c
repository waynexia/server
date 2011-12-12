/* Copyright (C) 2010 Sergei Golubchik and Monty Program Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/**
  @file

  dialog client authentication plugin with examples

  dialog is a general purpose client authentication plugin, it simply
  asks the user the question, as provided by the server and reports
  the answer back to the server. No encryption is involved,
  the answers are sent in clear text.

  Two examples are provided: two_questions server plugin, that asks
  the password and an "Are you sure?" question with a reply "yes, of course".
  It demonstrates the usage of "password" (input is hidden) and "ordinary"
  (input can be echoed) questions, and how to mark the last question,
  to avoid an extra roundtrip.

  And three_attempts plugin that gives the user three attempts to enter
  a correct password. It shows the situation when a number of questions
  is not known in advance.
*/
#include <my_global.h>
#include <mysql/client_plugin.h>
#include <mysql.h>
#include <string.h>

#if defined (_WIN32)
# define RTLD_DEFAULT GetModuleHandle(NULL)
#endif

#include <mysql/plugin_auth.h>

/**
  first byte of the question string is the question "type".
  It can be a "ordinary" or a "password" question.
  The last bit set marks a last question in the authentication exchange.
*/
#define ORDINARY_QUESTION       "\2"
#define LAST_QUESTION           "\3"
#define PASSWORD_QUESTION       "\4"
#define LAST_PASSWORD           "\5"

/********************* SERVER SIDE ****************************************/

/**
  dialog demo with two questions, one password and one ordinary.
*/

static int two_questions(MYSQL_PLUGIN_VIO *vio, MYSQL_SERVER_AUTH_INFO *info)
{
  unsigned char *pkt;
  int pkt_len;

  /* send a password question */
  if (vio->write_packet(vio,
                        (const uchar*) (PASSWORD_QUESTION "Password, please:"),
                        18))
    return CR_ERROR;

  /* read the answer */
  if ((pkt_len= vio->read_packet(vio, &pkt)) < 0)
    return CR_ERROR;

  info->password_used = 1;

  /* fail if the password is wrong */
  if (strcmp((char*) pkt, info->auth_string))
    return CR_ERROR;

  /* send the last, ordinary, question */
  if (vio->write_packet(vio,
                        (const uchar*) (LAST_QUESTION "Are you sure ?"), 15))
    return CR_ERROR;

  /* read the answer */
  if ((pkt_len= vio->read_packet(vio, &pkt)) < 0)
    return CR_ERROR;

  /* check the reply */
  return strcmp((char*) pkt, "yes, of course") ? CR_ERROR : CR_OK;
}

static struct st_mysql_auth two_handler=
{
  MYSQL_AUTHENTICATION_INTERFACE_VERSION,
  "dialog", /* requires dialog client plugin */
  two_questions
};


/**
  dialog demo where the number of questions is not known in advance
*/

static int three_attempts(MYSQL_PLUGIN_VIO *vio, MYSQL_SERVER_AUTH_INFO *info)
{
  unsigned char *pkt;
  int pkt_len, i;

  for (i= 0; i < 3; i++)
  {
    /* send the prompt */
    if (vio->write_packet(vio,
                          (const uchar*) (PASSWORD_QUESTION "Password, please:"), 18))
      return CR_ERROR;

    /* read the password */
    if ((pkt_len= vio->read_packet(vio, &pkt)) < 0)
      return CR_ERROR;

    info->password_used = 1;

    /*
      finish, if the password is correct.
      note, that we did not mark the prompt packet as "last"
    */
    if (strcmp((char*) pkt, info->auth_string) == 0)
      return CR_OK;
  }

  return CR_ERROR;
}

static struct st_mysql_auth three_handler=
{
  MYSQL_AUTHENTICATION_INTERFACE_VERSION,
  "dialog", /* requires dialog client plugin */
  three_attempts 
};

maria_declare_plugin(dialog)
{
  MYSQL_AUTHENTICATION_PLUGIN,
  &two_handler,
  "two_questions",
  "Sergei Golubchik",
  "Dialog plugin demo 1",
  PLUGIN_LICENSE_GPL,
  NULL,
  NULL,
  0x0100,
  NULL,
  NULL,
  "1.0",
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL
},
{
  MYSQL_AUTHENTICATION_PLUGIN,
  &three_handler,
  "three_attempts",
  "Sergei Golubchik",
  "Dialog plugin demo 2",
  PLUGIN_LICENSE_GPL,
  NULL,
  NULL,
  0x0100,
  NULL,
  NULL,
  "1.0",
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL
}
maria_declare_plugin_end;

/********************* CLIENT SIDE ***************************************/
/*
  This plugin performs a dialog with the user, asking questions and
  reading answers. Depending on the client it may be desirable to do it
  using GUI, or console, with or without curses, or read answers
  from a smardcard, for example.

  To support all this variety, the dialog plugin has a callback function
  "authentication_dialog_ask". If the client has a function of this name
  dialog plugin will use it for communication with the user. Otherwise
  a default gets() based implementation will be used.
*/
static mysql_authentication_dialog_ask_t ask;

static char *builtin_ask(MYSQL *mysql __attribute__((unused)),
                         int type __attribute__((unused)),
                         const char *prompt,
                         char *buf, int buf_len)
{
  fputs(prompt, stdout);
  fputc(' ', stdout);

  if (type == 2) /* password */
  {
    get_tty_password_buff("", buf, buf_len);
    buf[buf_len-1]= 0;
  }
  else
  {
    if (!fgets(buf, buf_len-1, stdin))
      buf[0]= 0;
    else
    {
      int len= strlen(buf);
      if (len && buf[len-1] == '\n')
        buf[len-1]= 0;
    }
  }

  return buf;
}


/**
  The main function of the dialog plugin.

  Read the prompt, ask the question, send the reply, repeat until
  the server is satisfied.

  @note
   1. this plugin shows how a client authentication plugin
      may read a MySQL protocol OK packet internally - which is important
      where a number of packets is not known in advance.
   2. the first byte of the prompt is special. it is not
      shown to the user, but signals whether it is the last question
      (prompt[0] & 1 == 1) or not last (prompt[0] & 1 == 0),
      and whether the input is a password (not echoed).
   3. the prompt is expected to be sent zero-terminated
*/

static int perform_dialog(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql)
{
  unsigned char *pkt, cmd= 0;
  int pkt_len, res;
  char reply_buf[1024], *reply;
  int first = 1;

  do
  {
    /* read the prompt */
    pkt_len= vio->read_packet(vio, &pkt);
    if (pkt_len < 0)
      return CR_ERROR;

    if (pkt == 0 && first)
    {
      /*
        in mysql_change_user() the client sends the first packet, so
        the first vio->read_packet() does nothing (pkt == 0).

        We send the "password", assuming the client knows what its doing.
        (in other words, the dialog plugin should be only set as a default
        authentication plugin on the client if the first question
        asks for a password - which will be sent in clear text, by the way)
      */
      reply= mysql->passwd;
    }
    else
    {
      cmd= *pkt++;

      /* is it MySQL protocol packet ? */
      if (cmd == 0 || cmd == 254)
        return CR_OK_HANDSHAKE_COMPLETE; /* yes. we're done */

      /*
        asking for a password in the first packet mean mysql->password, if it's set
        otherwise we ask the user and read the reply
      */
      if ((cmd >> 1) == 2 && first && mysql->passwd[0])
        reply= mysql->passwd;
      else
        reply= ask(mysql, cmd >> 1, (char*) pkt, reply_buf, sizeof(reply_buf));
      if (!reply)
        return CR_ERROR;
    }
    /* send the reply to the server */
    res= vio->write_packet(vio, (uchar*) reply, strlen(reply)+1);

    if (reply != mysql->passwd && reply != reply_buf)
      free(reply);

    if (res)
      return CR_ERROR;

    /* repeat unless it was the last question */
  } while ((cmd & 1) != 1);

  /* the job of reading the ok/error packet is left to the server */
  return CR_OK;
}


/**
  initialization function of the dialog plugin

  Pick up the client's authentication_dialog_ask() function, if exists,
  or fall back to the default implementation.
*/

static int init_dialog(char *errbuf __attribute__((unused)),
                       size_t sizeof_errbuf __attribute__((unused)),
                       int argc __attribute__((unused)),
                       va_list args __attribute__((unused)))
{
  void *sym= dlsym(RTLD_DEFAULT, "mysql_authentication_dialog_ask");
  ask= sym ? (mysql_authentication_dialog_ask_t)sym : builtin_ask;
  return 0;
}

mysql_declare_client_plugin(AUTHENTICATION)
  "dialog",
  "Sergei Golubchik",
  "Dialog Client Authentication Plugin",
  {0,1,0},
  init_dialog,
  NULL,
  perform_dialog
mysql_end_client_plugin;

