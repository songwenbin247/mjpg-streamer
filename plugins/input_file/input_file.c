/*******************************************************************************
#                                                                              #
#      MJPG-streamer allows to stream JPG frames from an input-plugin          #
#      to several output plugins                                               #
#                                                                              #
#      Copyright (C) 2007 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>
#include <syslog.h>
#include <fcntl.h>
//#include <limits.h>

#include "../../mjpg_streamer.h"
#include "../../utils.h"
#include "../input.h"
#include "../input_uvc/huffman.h"

#define INPUT_PLUGIN_NAME "input_file"
#define MAX_ARGUMENTS 32

/* private functions and variables to this plugin */
static pthread_t   worker;
static globals     *pglobal;

void *worker_thread( void *);
void worker_cleanup(void *);
void help(void);

static int delay = 1000;
static char *folder = NULL;
//static char *pipe_name = "/run/mjpg_streamer/input_file";
static char *pipe_name = "/run/mjpg/input_pipe";
int fd;

int is_huffman(unsigned char *buf)
{
  unsigned char *ptbuf;
  int i = 0;
  ptbuf = buf;
  while (((ptbuf[0] << 8) | ptbuf[1]) != 0xffda) {
    if (i++ > 2048)
      return 0;
    if (((ptbuf[0] << 8) | ptbuf[1]) == 0xffc4)
      return 1;
    ptbuf++;
  }
  return 0;
}

int memcpy_picture(unsigned char *out, unsigned char *buf, int size)
{
  unsigned char *ptdeb, *ptlimit, *ptcur = buf;
  int sizein, pos=0;

  if (!is_huffman(buf)) {
    ptdeb = ptcur = buf;
    ptlimit = buf + size;
    while ((((ptcur[0] << 8) | ptcur[1]) != 0xffc0) && (ptcur < ptlimit))
      ptcur++;
    if (ptcur >= ptlimit)
        return pos;
    sizein = ptcur - ptdeb;

    memcpy(out+pos, buf, sizein); pos += sizein;
    memcpy(out+pos, dht_data, sizeof(dht_data)); pos += sizeof(dht_data);
    memcpy(out+pos, ptcur, size - sizein); pos += size-sizein;
  } else {
    memcpy(out+pos, ptcur, size); pos += size;
  }
  return pos;
}

/*** plugin interface functions ***/
int input_init(input_parameter *param) {
  char *argv[MAX_ARGUMENTS]={NULL};
  int argc=1, i;

  /* convert the single parameter-string to an array of strings */
  argv[0] = INPUT_PLUGIN_NAME;
  if ( param->parameter_string != NULL && strlen(param->parameter_string) != 0 ) {
    char *arg=NULL, *saveptr=NULL, *token=NULL;

    arg=(char *)strdup(param->parameter_string);

    if ( strchr(arg, ' ') != NULL ) {
      token=strtok_r(arg, " ", &saveptr);
      if ( token != NULL ) {
        argv[argc] = strdup(token);
        argc++;
        while ( (token=strtok_r(NULL, " ", &saveptr)) != NULL ) {
          argv[argc] = strdup(token);
          argc++;
          if (argc >= MAX_ARGUMENTS) {
            IPRINT("ERROR: too many arguments to input plugin\n");
            return 1;
          }
        }
      }
    }
  }

  /* show all parameters for DBG purposes */
  for (i=0; i<argc; i++) {
    DBG("argv[%d]=%s\n", i, argv[i]);
  }

  reset_getopt();
  while(1) {
    int option_index = 0, c=0;
    static struct option long_options[] = \
    {
      {"h", no_argument, 0, 0},
      {"help", no_argument, 0, 0},
      {"d", required_argument, 0, 0},
      {"delay", required_argument, 0, 0},
      {"f", required_argument, 0, 0},
      {"folder", required_argument, 0, 0},
      {"p", required_argument, 0, 0},
      {0, 0, 0, 0}
    };

    c = getopt_long_only(argc, argv, "", long_options, &option_index);

    /* no more options to parse */
    if (c == -1) break;

    /* unrecognized option */
    if (c == '?'){
      help();
      return 1;
    }

    switch (option_index) {
      /* h, help */
      case 0:
      case 1:
        DBG("case 0,1\n");
        help();
        return 1;
        break;

      /* d, delay */
      case 2:
      case 3:
        DBG("case 2,3\n");
        delay = atoi(optarg);
        break;

      /* f, folder */
      case 4:
      case 5:
        DBG("case 4,5\n");
        folder = malloc(strlen(optarg)+2);
        strcpy(folder, optarg);
        if ( optarg[strlen(optarg)-1] != '/' )
          strcat(folder, "/");
        break;
      case 6:
	DBG("case 6\n");
	pipe_name = strdup(optarg);
	break;
      default:
        DBG("default case\n");
        help();
        return 1;
    }
  }

  pglobal = param->global;
  if ((fd = access(pipe_name, F_OK)) < 0){
  	fd = mkfifo(pipe_name, 0666);
	if (fd < 0){
  		IPRINT("Create pipe file %s fail\n", folder);		
		return 1;
	}
  }
  if ((fd = open(pipe_name, O_RDONLY)) < 0){
	fprintf(stderr, "Can't open pipe %s for reading\n", pipe_name);
	return -1;
  }
  IPRINT("JPG input folder..: %s\n", folder);
  IPRINT("delay.............: %i\n", delay);

  return 0;
}

int input_stop(void) {
  DBG("will cancel input thread\n");
  pthread_cancel(worker);
  close(fd);

  return 0;
}

int input_run(void) {
  pglobal->buf = malloc(1024 * 1024 * 2);
  if (pglobal->buf == NULL) {
    fprintf(stderr, "could not allocate memory\n");
    exit(1);
  }

  if( pthread_create(&worker, 0, worker_thread, NULL) != 0) {
    free(pglobal->buf);
    fprintf(stderr, "could not start worker thread\n");
    exit(EXIT_FAILURE);
  }
  pthread_detach(worker);

  return 0;
}

/*** private functions for this plugin below ***/
void help(void) {
    fprintf(stderr, " ---------------------------------------------------------------\n" \
                    " Help for input plugin..: "INPUT_PLUGIN_NAME"\n" \
                    " ---------------------------------------------------------------\n" \
                    " The following parameters can be passed to this plugin:\n\n" \
                    " [-d | --delay ]........: delay to pause between frames\n" \
                    " [-f | --folder ].......: folder containing the JPG-files\n" \
                    " ---------------------------------------------------------------\n");
}

#define BUFF_MAX (1024 * 256)
 unsigned char g_buf[BUFF_MAX + 12];
 int buf_free_len = BUFF_MAX;
 int buf_len = 0;
unsigned char *buf_index = g_buf;
unsigned char *buf_start = g_buf;
unsigned char *buf_end = g_buf + BUFF_MAX;


unsigned char * find_head(unsigned char *buf, int len)
{
	char *p;
	p = memchr(buf, 's', len);
	if (p)
		p =  *(p + 1) != 't' ? NULL : *(p + 2) != 'a' ? NULL : *(p + 3) != 'r' ?  NULL :  *(p + 4) != 't' ? NULL : p; 
	return (unsigned char *)p;
}

int read_a_frame()
{
	unsigned char *p;
	int frame_len;
	
	memcpy(buf_start, buf_index, buf_len);
	buf_index = buf_start + buf_len;

	while(1){
		buf_len += read(fd, buf_index, BUFF_MAX - buf_len + 12);
		buf_index = buf_start + buf_len;
		if (((p = find_head(buf_start, buf_len)) != NULL) && ( p <= (buf_start + buf_len - 12))){
			frame_len = (p[8] << 24) + (p[9] << 16) + (p[10] << 8) + p[11];
			buf_len = buf_len + buf_start - p - 12;
			memcpy(buf_start, p + 12, buf_len);
			break;
		}
		else if (p > (buf_start + buf_len - 12)){
			buf_len = p - (buf_start + buf_len - 12);
			memcpy(buf_start, p + 12, buf_len);
			buf_index = buf_start + buf_len;
		}else{
			buf_len = 0;
			buf_index = buf_start;
		}
	}
	while( frame_len > buf_len){
		buf_len += read(fd, buf_start + buf_len , BUFF_MAX - buf_len);
	}
	buf_index = buf_start + frame_len;
        buf_len	-= frame_len;
	return frame_len;
}

/* the single writer thread */
void *worker_thread( void *arg ) {
  /* set cleanup handler to cleanup allocated ressources */
  pthread_cleanup_push(worker_cleanup, NULL);
  int len;
  while( !pglobal->stop ) {

    /* grab a frame */
    if ((len = read_a_frame()) < 0) {
      fprintf(stderr, "Error grabbing frames\n");
      exit(EXIT_FAILURE);
    }
    /* copy JPG picture to global buffer */
    pthread_mutex_lock( &pglobal->db );
    pglobal->size = len; 
     memcpy_picture(pglobal->buf, g_buf, len);

    /* signal fresh_frame */
    pthread_cond_broadcast(&pglobal->db_update);
    pthread_mutex_unlock( &pglobal->db );
//usleep(1000*delay);

  }

  DBG("leaving input thread, calling cleanup function now\n");
  pthread_cleanup_pop(1);
  return NULL;
}

void worker_cleanup(void *arg) {
  static unsigned char first_run=1;

  if ( !first_run ) {
    DBG("already cleaned up ressources\n");
    return;
  }

  first_run = 0;
  DBG("cleaning up ressources allocated by input thread\n");

  if (pglobal->buf != NULL) free(pglobal->buf);
}




