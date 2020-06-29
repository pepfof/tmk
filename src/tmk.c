#include <config.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
//#include <termios.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include <stdbool.h> 
#include <time.h>
#include <ncurses.h>

static snd_seq_t *handle;
static int tmk_client;
static int tmk_port = 0;
static int dest_client;
static int dest_port;
static snd_seq_event_t *ev;
static int queue;
static int output_ret;
static unsigned char octave = 5;

#define WIDTH 30
#define HEIGHT 10 

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

double notetime[128]={ -1 };
bool noteon[128]={ 0 };
long int notetimer = 100;
clock_t lasttime = 0;

char cmdinput[4];
static bool tmk_inputcommand(){
	int i = 0;
	bool ch = 0;
	char c = getch();
	if(c==ERR){c=0;}
	if(c!=0){mvprintw(0, 0, "%d", c);}
	if(c == 10){
			clear();
			mvprintw(1, 0, "%s", cmdinput); return 1;}
	while(i<4 && ch == 0){
		if(cmdinput[i]==0){cmdinput[i]=c; ch = 1;}
		i++;
	}
	return 0;
}

static void tmk_cleannotes(){
	clock_t newtime = clock();
	double mselapsed = ((newtime-lasttime)/CLOCKS_PER_SEC)*1000;
	//printf("%f", mselapsed);
	int i = 0;
	while(i<128){
		if(notetime[i]>0 && !noteon[i]){notetime[i]=-1; noteon[i]=0; send_note_off(i);}
		if(notetime[i]<=0 && noteon[i]){notetime[i]=-1; noteon[i]=0; send_note_off(i);}
		if(notetime[i]>0){notetime[i]-=mselapsed;}
		i++;
	}
	lasttime = newtime;
}

static int tmk_intepret(char opcode[4]){
		switch(opcode[0]){
		case 'n':
			mvprintw(3, 0, "note %c @ %d %c", opcode[1], octavetranslate(opcode[2]), opcode[3]);
			switch(opcode[3]){
			case '!':
				send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
				break;
			case 'X' | 'x':
				send_note_off(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=0;
				break;
			case '#':
				send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
				notetime[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]= notetimer;
				break;
			case 'T':
				if(noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]) {
					send_note_off(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
					noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=0;
					}
				else{
					send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
					noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
					} break;
			default: 
				send_note_on(12 * octavetranslate(opcode[2])+notetranslate(opcode[1]));
				noteon[12 * octavetranslate(opcode[2])+notetranslate(opcode[1])]=1;
				break;
				}
			break;
		case 'o':
			octave=octavetranslate(opcode[1]);
			break;
		case 'q':
			printf("Exiting\r\n");
			fflush(stdout);
			do_exit();
			break;
/*		case 'p':
			printf("Pausing\r\n");
			unsigned char in_ch;
			//cur_kb_mode = (char *) K_XLATE;
			//ioctl(STDIN_FILENO, KDSKBMODE, K_XLATE);
			int done = 0;
			while(1) {
				read(STDIN_FILENO, &in_ch , 1);
				switch(in_ch) {
				case '\t':
			 		done = 1;
					break;
				case 'q':
					printf("Exiting\r\n");
					fflush(stdout);
					do_exit();
					break;
				default:
					break;
				}
				if (done) {
					break;
				}
			}
			printf("Resuming\r\n");
			cur_kb_mode = (char *) K_RAW;
			//ioctl(STDIN_FILENO, KDSKBMODE, K_RAW);
			break;*/
		default:
			break;	
		}
	}

int main(int argc, char *argv[])
{
	seq_init();
	if (argc > 2) {
		printf("Too many arguments.\r\n");
		usage(argv[0]);
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

	WINDOW *menu_win;
	initscr();
	cbreak();
	menu_win = newwin(HEIGHT, WIDTH, starty, startx);
	keypad(menu_win, TRUE);
	int keyn = 5; //defining the amount of keys we have, should be easily readable and changeable from config
	char remappingtable_in[]={0xac, 0xad, 0x1e,0x11,0x1f}; //defining what scancodes to parse, with the release scancodes being auto-offset by 128. Ditto about config.
	char remappingtable_out[][4]={"od  ","ou  ","nC##","nd##","nE##"}; //defining output to the interpreter, directly correlating with the index of the button that has been pressed. Config thing.
	bool quit = 0;
	bool ch = 0;
	char temp_input[5] = {0,0,0,0};
        while(!quit) {
		//scanf("%s", &temp_input);
		ch = tmk_inputcommand();
		if(ch){ch = 0;
		int i = 0;
		while(i<4){
			temp_input[i]=cmdinput[i];
			cmdinput[i]=0;
			i++;
		}
		mvprintw(2, 0, "%s", temp_input);
		tmk_intepret(temp_input);
		i = 0;
		while(i<4){
			temp_input[i]=0;
			i++;
		}}
		tmk_cleannotes();
	}
}
