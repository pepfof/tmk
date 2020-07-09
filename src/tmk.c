#include <config.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
//#include <termios.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include <stdbool.h> 
#include <sys/time.h>
#include <ncurses.h>
#include <inttypes.h>
#include <math.h>

static snd_seq_t *handle;
static int tmk_client;
static int tmk_port = 0;
static int dest_client;
static int dest_port;
static snd_seq_event_t *ev;
static int queue;
static int output_ret;
static unsigned char octave = 5;

int lengthx = 40;
int lengthy = 40;
int startx = 0;
int starty = 0;

//static struct termios orig_term;
//static struct termios raw;
static char * cur_kb_mode;

static void send_note_on(unsigned char note) {
	ev->type = SND_SEQ_EVENT_NOTEON;
	ev->data.note.note = note;
	ev->data.note.velocity = 127;
	snd_seq_ev_set_subs(ev);
	snd_seq_event_output_direct(handle, ev);
}

static void send_note_off(unsigned char note) {
	ev->type = SND_SEQ_EVENT_NOTEOFF;
	ev->data.note.note = note;
	ev->data.note.velocity = 0;
	snd_seq_ev_set_subs(ev);
	snd_seq_event_output_direct(handle, ev);
}

static void seq_init() {
	snd_seq_open(&handle, "default", SND_SEQ_OPEN_OUTPUT, 0);
	if (handle == NULL) {
		printf("Could not allocate sequencer.\r\n");
		exit(1);
	}
	snd_seq_set_client_name(handle, "tmk");
	snd_seq_create_simple_port(handle, "Output",
			SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC);
}

static void do_exit() {
//	terminal_teardown();
	endwin();
	_exit(0);
}

static void usage(char *cmd) {
	printf("TTY MIDI Keyboard\r\n");
	printf("Usage:\r\n");
	printf("\t%s [dest_client:dest_port]\r\n", cmd);
	printf("\t%s (-h | --help)\r\n", cmd);
	fflush(stdout);
}

static char notetranslate(char a, bool reverse){
	char notesindexed[] = {'c', 'C', 'd', 'D', 'e', 'f', 'F', 'g','G','a','A','b'};
	int i = 0;
	if(reverse && a>=0 && a<12) return notesindexed[a];
	else if(reverse) return 0;
	while(i<12){
		if (a==notesindexed[i]) return i;
		i++;
	}
	return 0;
}

static int octavetranslate(char a){
	int octaveresult = octave;
	if(a == '#') octaveresult=octave;
	else if(a>='0' && a<='9') octaveresult = (a-'0');
	else if(a>='a' && a<='z') { 
		if((octave-(a+1-'a')<0)){octaveresult=0;}
		else{octaveresult=octave-(a+1-'a');}
	}
	else if(a>='A' && a<='Z'){
		if((octave+(a+1-'A'))>10){octaveresult=10;}
		else{octaveresult=octave+(a+1-'A');}
	}
	return octaveresult;
}

static int numbertranslate(char a, int power){
	if(a>='0' && a<='9') return (a-'0')*(pow(64, power));
	else if(a>='a' && a<='z') return (a-'a' + 10)*(pow(64, power));
	else if(a>='A' && a<=93) return (a-'A' + 36)*(pow(64, power));
	return 0;
}

int in_ch=0;

char cmdinput[4];
static bool tmk_inputcommand(WINDOW *mw, int x, int y){ //the display code is temporary
	int i = 0; int j = 4;
	bool ch = 0; bool backspace = 0;
	in_ch = getch();
	if(in_ch==ERR){in_ch=0;}
	if(in_ch == 10){
			mvprintw(y, x+1, "    ");
			//mvprintw(1, 0, "collected: %s", cmdinput);
		return 1;
		}
	if(in_ch==127){
			backspace = 1; /*mvprintw(0, 5, "<-");*/
		}
	while(i<4 && ch == 0 && backspace==0){
		if(cmdinput[i]==0 | (i==3 && in_ch!=0)){
			cmdinput[i]=in_ch;
			ch = 1;
			wmove(mw, y, x+i+1);
			}
		i++;
	}
	while(j>=0 && ch == 0 && backspace==1){
		if(cmdinput[j]!=0){
			cmdinput[j]=0;
			mvprintw(y, x+j+1, " ");
			wmove(mw, y, x+j+1);
			ch = 1;
			backspace=0;
			}
		j--;
	}
	if(in_ch!=0 || 1){
		mvprintw(y, x, "[    ]", cmdinput);
		mvprintw(y, x+1, "%s", cmdinput);}
	return 0;
}


bool notetimeron = 0;
double notetime[128]={ 0 };
char noteon[128]={ 0 };
long int notetimer = 1000;
long long int lasttime = 0;
long long int newtime = 0;

char reportstring[50]="Started!";
char lastreport[50];

static bool tmk_autonotes(long long int curtime){
	newtime = curtime;
	long long int mselapsed = (newtime-lasttime);
	bool checkno = 0;
	int i = 0;
	while(i<128){
		if(notetime[i]>=0 && noteon[i]==2){
			notetime[i]-=mselapsed;
		}
		if(notetime[i]<0 && noteon[i]==2){
			notetime[i] = 0; 
			noteon[i]=0;
			send_note_off(i);
			sprintf(reportstring, "note %c @ %d autooff", notetranslate(i%12,1),i/12);
		}
		if(notetime != 0 && noteon[i]!=2){
			notetime[i]=0;
		}
		//if(noteon[i]==2){mvprintw(4, i, "a"); checkno = 1;}
		/*mvprintw(7, i, "%d", checkno);
		mvprintw(9, i, "%d", notetime[i]);
 		mvprintw(8, i, "%d", noteon[i]);*/
		i++;
	}
	lasttime = newtime;
	return checkno;
}


static int tmk_intepret(char opcode[4]){
		switch(opcode[0]){
		case 'n':
			sprintf(reportstring, "note %c @ %d %c", opcode[1], octavetranslate(opcode[2]), opcode[3]);
			char lnote = notetranslate(opcode[1],0); char locta = octavetranslate(opcode[2]);
			switch(opcode[3]){
			case '!':
				send_note_on(12 * locta+lnote);
				noteon[12 * locta+lnote]=1;
				break;
			case 'X' | 'x':
				send_note_off(12 * locta+lnote);
				noteon[12 * locta+lnote]=0;
				break;
			case '#':
				send_note_on(12 * locta+lnote);
				noteon[12 * locta+lnote]=2;
				notetime[12 * locta+lnote]= notetimer;
				notetimeron=1;
				break;
			case 'T' | 't':
				if(noteon[12 * locta+lnote]) {
					send_note_off(12 * locta+lnote);
					noteon[12 * locta+lnote]=0;
					}
				else{
					send_note_on(12 * locta+lnote);
					noteon[12 * locta+lnote]=1;
					} break;
			default: mvprintw(0, 0, "%s", cmdinput);
				send_note_on(12 * locta+lnote);
				noteon[12 * locta+lnote]=1;
				break;
				}
			break;
		case 'd':
			notetimer=numbertranslate(opcode[1],2)+numbertranslate(opcode[2],1)+numbertranslate(opcode[3],0);
			sprintf(reportstring, "notetimer = %ldms", notetimer);
			break;
		case 'o':
			octave=octavetranslate(opcode[1]);
			sprintf(reportstring, "octave = %d", octave);
			break;
		case 'q':
			usleep(1000000);
			fflush(stdout);
			do_exit();
			break;
		default:
			break;	
		}
	}


static void tmk_report(bool decoration, int x, int y){
if(strcmp(reportstring, lastreport)!=0){
	clear();
	if(decoration){
		mvprintw(y,x,"TMK REPORTS:");
		mvprintw(y,x+13,"%s", reportstring);
	}
	else{
		mvprintw(y,x,"%s", reportstring);
	}
}
	strcpy(lastreport,reportstring);
}

int main(int argc, char *argv[]){
	seq_init();
	if (argc > 2) {mvprintw(0, 0, "%s", cmdinput);
		do_exit();
	}
	if (argc == 2) {
		if (sscanf(argv[1],"%d:%d",&dest_client,&dest_port) == 2) {
			tmk_client = snd_seq_client_id(handle);
			snd_seq_addr_t tmk, dest;
			snd_seq_port_subscribe_t *sub;
			tmk.client = tmk_client;
			tmk.port = tmk_port;
			dest.client = dest_client;
			dest.port = dest_port;
			snd_seq_port_subscribe_alloca(&sub);
			snd_seq_port_subscribe_set_sender(sub, &tmk);
			snd_seq_port_subscribe_set_dest(sub, &dest);
			if (snd_seq_subscribe_port(handle, sub) < 0) {
				printf("Error connecting to midi client: %s\r\n", argv[1]);
				fflush(stdout);
				do_exit();
			}
		}
		else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
			usage(argv[0]);
			do_exit();
		}
		else {
			printf("Unknown argument: %s\r\n", argv[1]);
			usage(argv[0]);
			do_exit();
		}
	}
	ev = malloc(sizeof(snd_seq_event_t));
	if (ev == NULL) {
		printf("Could not allocate midi event.\r\n");
		fflush(stdout);
		do_exit();
	}
	snd_seq_ev_set_direct(ev);
	snd_seq_ev_set_source(ev, tmk_port);
	ev->data.note.channel = 1;
	ev->data.note.velocity = 127;
	
	long long int gllasttime = 0;
	long long int glcurtime = 0;
	struct timespec timems;

	WINDOW *menu_win;
	initscr();
	menu_win = newwin(lengthx, lengthy, starty, startx);
	keypad(menu_win, TRUE);
	bool quit = 0;
	bool ch = 0;
	nodelay(menu_win, 1);
	timeout(0);
	refresh();
	noecho();
	//nice(80);
	curs_set(2);
	char temp_input[5] = {0,0,0,0};
        while(!quit) {
		ch = tmk_inputcommand(menu_win, 0, 0);
		if(ch){ch = 0;
		int i = 0;
		while(i<4){
			temp_input[i]=cmdinput[i];
			cmdinput[i]=0;
			i++;
		}
		tmk_intepret(temp_input);
		i = 0;
		while(i<4){
			temp_input[i]=0;
			i++;
		}}
		clock_gettime(CLOCK_MONOTONIC, &timems);
		glcurtime=timems.tv_sec*1000+timems.tv_nsec/1000000;
		if(glcurtime-gllasttime>=1){
		tmk_autonotes(glcurtime);
		tmk_report(1, 0, 1);
		gllasttime=glcurtime;}
		wrefresh(menu_win);
		//mvprintw(5,0,"%d",glcurtime);
		usleep(10000);
	}
}
