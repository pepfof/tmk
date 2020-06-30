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

#define WIDTH 128
#define HEIGHT 50 

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

static int notetranslate(char a){
	char notesindexed[] = {'c', 'C', 'd', 'D', 'e', 'f', 'F', 'g','G','a','A','b'};
	int i = 0;
	while(i<12){
		if (a==notesindexed[i]) return i;
		i++;
	}
	return 0;
}

static int octavetranslate(char a){
	if(a == '#') return octave;
	else if(a>='0' && a<='9') return (a-'0');
	else if(a>='a' && a<='z') return (octave-(a+1-'a'));
	else if(a>='A' && a<='Z') return (octave+(a+1-'A'));
	else return 5;
}

static int numbertranslate(char a, int power){
	if(a>='0' && a<='9') return (a-'0')*(pow(64, power));
	else if(a>='a' && a<='z') return (a-'a' + 10)*(pow(64, power));
	else if(a>='A' && a<=93) return (a-'A' + 36)*(pow(64, power));
	return 0;
}

int in_ch=0;

char cmdinput[4];
static bool tmk_inputcommand(WINDOW *mw){ //the display code is temporary
	int i = 0; int j = 4;
	bool ch = 0; bool backspace = 0;
	in_ch = getch();
	if(in_ch==ERR){in_ch=0;}
	if(in_ch == 10){
			clear();
			mvprintw(1, 0, "collected: %s", cmdinput); return 1;}
	if(in_ch==127){backspace = 1; mvprintw(0, 30, "backspace removes");}
	while(i<4 && ch == 0 && backspace==0){
		if(cmdinput[i]==0 | (i==3 && in_ch!=0)){
			cmdinput[i]=in_ch;
			ch = 1;
			wmove(mw, 0, i);
			}
		i++;
	}
	while(j>=0 && ch == 0 && backspace==1){
		if(cmdinput[j]!=0){
			mvprintw(0, 60, "%c", cmdinput[j]);
			cmdinput[j]=0;
			mvprintw(0, j, " ");
			wmove(mw, 0, j);
			ch = 1;
			backspace=0;
			}
		j--;
	}
	if(in_ch!=0){
		mvprintw(0, 0, "%s", cmdinput);}
	return 0;
}

double notetime[128]={ 0 };
char noteon[128]={ 0 };
long int notetimer = 1000;
clock_t lasttime = 0;
clock_t newtime = 0;
static clock_t tmk_cleannotes(clock_t curtime){
	newtime = curtime;
	clock_t mselapsed = (newtime-lasttime);
	int i = 0;
	while(i<128){
		if(notetime[i]>=0 && noteon[i]==2){notetime[i]-=mselapsed;}
		if(notetime[i]<0 && noteon[i]==2){notetime[i] = 0; noteon[i]=0;send_note_off(i);}
		if(notetime != 0 && noteon[i]!=2){notetime[i]=0;}
		//mvprintw(9, i, "%d", notetime[i]);
 		//mvprintw(8, i, "%d", noteon[i]);
		i++;
	}
	lasttime = newtime;
	return mselapsed;
}

static int tmk_intepret(char opcode[4]){
		switch(opcode[0]){
		case 'n':
			mvprintw(3, 0, "note %c @ %d %c", opcode[1], octavetranslate(opcode[2]), opcode[3]);
			switch(opcode[3]){
			case '!':
				mvprintw(3, 0, "note %c @ %d %c", opcode[1], octavetranslate(opcode[2]), opcode[3]);
				send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
				break;
			case 'X' | 'x':
				send_note_off(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=0;
				break;
			case '#':
				send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=2;
				notetime[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]= notetimer;
				break;
			case 'T' | 't':
				if(noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]) {
					send_note_off(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
					noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=0;
					}
				else{
					send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
					noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
					} break;
			default: mvprintw(0, 0, "%s", cmdinput);
				send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
				break;
				}
			break;
		case 'd':
			notetimer=numbertranslate(opcode[1],2)+numbertranslate(opcode[2],1)+numbertranslate(opcode[3],0);
			mvprintw(3, 0, "notetimer = %dms", notetimer);
			break;
		case 'o':
			octave=octavetranslate(opcode[1]);
			break;
		case 'q':
			printf("Exiting\r\n");
			fflush(stdout);
			do_exit();
			break;
		default:
			break;	
		}
	}

int main(int argc, char *argv[])
{
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
	
	clock_t gllasttime = 0;
	clock_t glcurtime = 0;

	WINDOW *menu_win;
	initscr();
	menu_win = newwin(HEIGHT, WIDTH, starty, startx);
	keypad(menu_win, TRUE);
	bool quit = 0;
	bool ch = 0;
	nodelay(menu_win, 1);
	timeout(0);
	refresh();
	noecho();
	char temp_input[5] = {0,0,0,0};
        while(!quit) {
		//mvprintw(8, 0, "%d", clock());
		//scanf("%s", &temp_input);
		ch = tmk_inputcommand(menu_win);
		if(ch){ch = 0;
		int i = 0;
		while(i<4){
			temp_input[i]=cmdinput[i];
			cmdinput[i]=0;
			i++;
		}
		mvprintw(2, 0, "passed to interp: %s", temp_input);
		tmk_intepret(temp_input);
		i = 0;
		while(i<4){
			temp_input[i]=0;
			i++;
		}}
		glcurtime=clock()/(CLOCKS_PER_SEC/1000);
		if(glcurtime-gllasttime>1){
		mvprintw(10,0,"%d",tmk_cleannotes(glcurtime));
		gllasttime=glcurtime;}
		wrefresh(menu_win);
	}
}
