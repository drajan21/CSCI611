#include "goldchase.h"
#include "Map.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <fstream> 
#include <iostream>
#include <errno.h>
#include <stdlib.h>
#include <semaphore.h>
#include <vector>
#include <signal.h>
#include <mqueue.h>


struct GameBoard {
        int columns;
        int rows;
        unsigned char players;
        pid_t player_process[5];
        char board[0];
};

typedef struct GameBoard gb; 

using namespace std;

Map *mapptr;
mqd_t readqueue_fd;
string mqname;
gb *gbaddr;
int oldIndex;

//function to get the player id
string get_playerId()
{
        pid_t pid = getpid();
        string player;
        for(int i=0 ;i <5; ++i)
        {
                if(gbaddr->player_process[i] == pid)
                        player = to_string(i+1); 
        }
        return player;
}

void cleanUp()
{
        string playerId = get_playerId();
        char cur = 0;
        int process = atoi(playerId.c_str());

        if(playerId == "1")
                cur = G_PLR0;
        else if(playerId == "2")
                cur = G_PLR1;
        else if(playerId == "3")
                cur = G_PLR2;
        else if(playerId == "4")
                cur = G_PLR3;
        else if(playerId == "5")
                cur = G_PLR4;

        gbaddr->players &= ~cur;
        gbaddr->board[oldIndex] &= ~cur;
        gbaddr->player_process[process - 1] = 0;

        mapptr->drawMap();
        for(int i =0 ; i < 5; i++)
        { 
                pid_t pid=getpid();
                if(gbaddr->player_process[i] != 0 && gbaddr->player_process[i]!=pid)
                {
                        if(kill(gbaddr->player_process[i],SIGUSR1)==-1)  
                                perror("Ouch on kill");
                }
        }
        mq_close(readqueue_fd);
        mq_unlink(mqname.c_str());
        //if all players have quit 
        if(gbaddr->players == 0)
        {
                shm_unlink("/sharedregion_DR");
                sem_unlink("/goldChaseSemaphore_DR");
        } 
}

void handle_mapCleanUp(int signum)
{
        cleanUp();
        throw(0);
}

void handle_mapChange(int signum)
{
        if(mapptr)
                mapptr->drawMap();
}

void read_message(int signum)
{

        //set up message queue to receive signal whenever
        //message comes in
        struct sigevent mq_notification_event;
        mq_notification_event.sigev_notify=SIGEV_SIGNAL;
        mq_notification_event.sigev_signo=SIGUSR2;
        mq_notify(readqueue_fd, &mq_notification_event);

        //read a message
        int err;
        char msg[121];
        memset(msg, 0, 121);//set all characters to '\0'
        while((err=mq_receive(readqueue_fd, msg, 120, NULL))!=-1)
        {
                mapptr->postNotice(msg);
                memset(msg, 0, 121);//set all characters to '\0'
        }
        //we exit while-loop when mq_receive returns -1
        //if errno==EAGAIN that is normal: there is no message waiting
        if(errno!=EAGAIN)
        {
                perror("mq_receive");
                exit(1);
        }
}

//function to send message
void send_msg(int process, string msg)
{
        string write_mqname;  
        switch(process)
        {
                case 0: write_mqname = "/drmq1";
                        break;

                case 1: write_mqname = "/drmq2";
                        break;

                case 2: write_mqname = "/drmq3";
                        break;

                case 3: write_mqname = "/drmq4";
                        break;

                case 4: write_mqname = "/drmq5";
                        break;

        }
        mqd_t writequeue_fd;
        if((writequeue_fd =  mq_open(write_mqname.c_str(), O_WRONLY|O_NONBLOCK)) == -1)
        {
                perror("mq open error at write mq");
                exit(1);
        }
        char message_text[121];
        memset(message_text , 0 ,121);
        strncpy(message_text, msg.c_str(), 121);

        if(mq_send(writequeue_fd, message_text, strlen(message_text), 0) == -1)
        {
                perror("mqsend error");
                exit(1);
        }
        mq_close(writequeue_fd);
}

void broadcast_msg(string msg, gb * gbadd, int cur_player,int errorMsgFlag)
{
        unsigned int player_mask = gbadd->players;
        player_mask &= ~cur_player;

        if(player_mask == 0 ) 
        {
                if(errorMsgFlag == true)
                {
                        mapptr->postNotice("Error: no players to send message to!"); 
                }
        }
        else
        {
                if(msg == "")
                        msg = mapptr->getMessage();
                if(errorMsgFlag) 
                        msg = "Player " + get_playerId() + " says: " + msg;
                pid_t pid = getpid();

                for(int i=0 ; i < 5; ++i)
                {
                        if(gbadd->player_process[i] != 0 && gbadd->player_process[i] != pid)
                        {
                                send_msg(i,msg);
                        }
                }
        }
}

int main()
{
        try{
                srand(time(0));

                //struct for handling SIGINT,SIGTERM,SIGHUP
                struct sigaction mapCleanUp;
                mapCleanUp.sa_handler = handle_mapCleanUp;
                sigemptyset(&mapCleanUp.sa_mask);
                mapCleanUp.sa_flags = 0;

                sigaction(SIGINT,&mapCleanUp,NULL);
                sigaction(SIGTERM,&mapCleanUp,NULL);
                sigaction(SIGHUP,&mapCleanUp,NULL);

                //struct for SIGUSR1
                struct sigaction mapChange_handler;
                mapChange_handler.sa_handler = handle_mapChange;
                sigemptyset(&mapChange_handler.sa_mask);
                mapChange_handler.sa_flags = 0;

                sigaction(SIGUSR1,&mapChange_handler,NULL);

                //struct for SIGUSR2
                struct sigaction action_to_take;
                action_to_take.sa_handler =  read_message;
                sigemptyset(&action_to_take.sa_mask);
                action_to_take.sa_flags = 0;

                sigaction(SIGUSR2,&action_to_take,NULL);

                struct mq_attr mq_attributes;
                mq_attributes.mq_flags = 0;
                mq_attributes.mq_maxmsg = 10;
                mq_attributes.mq_msgsize =  120;

                int numgold = 0;
                char c = ' ';
                int bytes = 0;
                int noLines = 0;
                string line;
                char *len;
                int fd;
                char cur_player = 0;

                string playerId;
                bool draw = false;

                sem_t *my_sem_ptr;
                //Creating semaphore in Read/Write mode to check if one has been created yet
                my_sem_ptr = sem_open("/goldChaseSemaphore_DR",O_RDWR,S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH,1);
                //1st player
                if(my_sem_ptr == SEM_FAILED)
                {
                        //Create Shared Memory
                        my_sem_ptr = sem_open("/goldChaseSemaphore_DR", O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH,1);
                        ifstream inputMap("mymap.txt");
                        inputMap >> numgold;
                        inputMap.ignore();

                        vector <string> mp_vector;
                        int c;
                        while(getline(inputMap,line))
                        {
                                mp_vector.push_back(line);
                                bytes += (unsigned)line.length();
                                c = line.length();
                                ++noLines;
                        }
                        inputMap.clear();
                        sem_wait(my_sem_ptr);

                        fd = shm_open("/sharedregion_DR", O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH);
                        ftruncate(fd, bytes + sizeof(gb));
                        /* Map the memory object */
                        gbaddr = (gb *) mmap(0, bytes + sizeof(gb), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

                        cur_player = G_PLR0;
                        playerId = "1";
                        gbaddr->players |= G_PLR0;
                        gbaddr->rows = noLines;
                        gbaddr->columns = c;
                        char* mp = gbaddr->board;

                        for(int i=0;i<mp_vector.size();i++)
                        {
                                string ln = mp_vector[i];
                                for(int j=0;j<ln.length();j++)
                                {
                                        switch(ln[j])
                                        {
                                                case ' ': *mp = 0;
                                                          ++mp;
                                                          break;

                                                case '*': *mp = G_WALL;
                                                          ++mp;
                                                          break;
                                        }
                                }
                        }

                        c = ' ';
                        int counter = 0;

                        inputMap.close();

                        int fGold = numgold - 1;
                        int rVal,z;
                        //randomly place FOOLs GOLD
                        for(int i=0; i < fGold; ++i)
                        {
                                z = 0;
                                while(z == 0)
                                {
                                        rVal = rand() % bytes;
                                        if(gbaddr->board[rVal] ==0)
                                        {
                                                z = 1;
                                                gbaddr->board[rVal] = G_FOOL;
                                        }
                                }
                        }

                        z = 0;
                        //randomly place GOLD
                        while(z == 0)
                        {
                                rVal = rand() % bytes;
                                if(gbaddr->board[rVal] ==0)
                                {
                                        z = 1;
                                        gbaddr->board[rVal] = G_GOLD;
                                }
                        }
                        sem_post(my_sem_ptr);

                        mqname = "/drmq1";
                        for(int i=0; i < 5 ; i++)
                        {
                                gbaddr->player_process[i] = 0;
                        }
                        gbaddr->player_process[0] = getpid();
                }
                //Any other players
                else
                {
                        fd = shm_open("/sharedregion_DR", O_RDWR, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH);
                        if(fd == -1)
                        {
                                cerr<< "Error!!!";
                                exit(1);
                        }

                        int c;
                        int r;
                        sem_wait(my_sem_ptr);
                        read(fd,&c,sizeof(int));
                        read(fd,&r,sizeof(int));
                        gbaddr = (gb *) mmap(0, (r*c) + sizeof(gb), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

                        if((gbaddr->players & G_PLR0) != G_PLR0)
                        {
                                cur_player = G_PLR0;
                                playerId = "1";
                                gbaddr->player_process[0] = getpid();
                                mqname = "/drmq1";
                        }
                        else if((gbaddr->players & G_PLR1) != G_PLR1)
                        {   
                                cur_player = G_PLR1;
                                playerId = "2";
                                gbaddr->player_process[1] = getpid();
                                mqname = "/drmq2";
                        }
                        else if((gbaddr->players & G_PLR2) != G_PLR2)
                        {
                                cur_player = G_PLR2;
                                playerId = "3";
                                gbaddr->player_process[2] = getpid();
                                mqname = "/drmq3";
                        }
                        else if((gbaddr->players & G_PLR3) != G_PLR3)
                        {
                                cur_player = G_PLR3;
                                playerId = "4";
                                gbaddr->player_process[3] = getpid();
                                mqname = "/drmq4";
                        }
                        else if((gbaddr->players & G_PLR4) != G_PLR4)
                        {
                                cur_player = G_PLR4;
                                playerId = "5";
                                gbaddr->player_process[4] = getpid();
                                mqname = "/drmq5";
                        }
                        //if 5 players are already playing the game
                        else
                        {
                                cerr << "Game not available now" << endl;
                                sem_post(my_sem_ptr);
                                exit(1);
                        }
                        gbaddr->players |= cur_player; 
                        sem_post(my_sem_ptr); 
                }
                sem_wait(my_sem_ptr); 

                int rval; 
                int x = 0;
                while(x == 0)
                {
                        rval = rand() % (gbaddr->rows * gbaddr->columns);

                        if(gbaddr->board[rval] ==0)
                        {
                                x = 1;
                                gbaddr->board[rval] = cur_player;
                        }
                }
                Map goldMine((const unsigned char *)gbaddr->board,gbaddr->rows,gbaddr->columns);
                mapptr = &goldMine;
                // mapptr = new Map((const unsigned char *)gbaddr->board, gbaddr->rows, gbaddr->columns);
                for(int i =0 ; i < 5; i++)
                {
                        pid_t pid=getpid();
                        if(gbaddr->player_process[i] != 0 && gbaddr->player_process[i]!=pid)
                        {
                                if(kill(gbaddr->player_process[i],SIGUSR1) == -1)
                                        perror("SIGUSR1 error!");
                        }
                }
                if((readqueue_fd = mq_open(mqname.c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
                                                S_IRUSR|S_IWUSR, &mq_attributes)) == -1)
                {
                        perror("mq_open");
                        exit(1);
                }
                struct sigevent mq_notification_event;
                mq_notification_event.sigev_notify=SIGEV_SIGNAL;
                mq_notification_event.sigev_signo=SIGUSR2;
                mq_notify(readqueue_fd, &mq_notification_event);


                sem_post(my_sem_ptr);

                int a=0;
                int key;
                int cur_row = 0;
                int cur_col = 0;
                int col = 0;
                int row = 0;
                int newIndex = 0;

                oldIndex = rval;
                cur_col = rval % gbaddr->columns;   
                cur_row = rval / gbaddr->columns;
                mapptr->postNotice("This is a notice");
                bool gotGold = false;
                bool gotFoolsGold = false;
                bool gotGoldPost = false;

                key = mapptr->getKey();

                char tmp[100];

                while(key != 'Q')
                {
                        if((gbaddr->columns == 1)  &&(cur_row==0 || cur_row == gbaddr->rows-1) &&(gotGold == true))
                        {
                                string msg = "Player " + playerId + " has Won!!!";
                                mapptr->postNotice("You have Won!!!");
                                broadcast_msg(msg,gbaddr,cur_player,false);
                                break;
                        }
                        else if((gbaddr->rows == 1)  &&(cur_col==0 || cur_col == gbaddr->columns-1) &&(gotGold == true))
                        {
                                string msg = "Player " + playerId + " has Won!!!";
                                mapptr->postNotice("You have won!!!");
                                broadcast_msg(msg,gbaddr,cur_player,false);
                                break;
                        }

                        //left key
                        if(key == 'h' && gotGold == true && cur_col == 0)
                        {
                                mapptr->postNotice("You have Won!!!");
                                string msg = "Player " + playerId + " has Won!!!";
                                broadcast_msg(msg,gbaddr,cur_player,false);
                                break;
                        }
                        else if(key == 'l' && gotGold == true && cur_col == gbaddr->columns-1)
                        {
                                mapptr->postNotice("You have Won!!!");
                                string msg = "Player " + playerId + " has Won!!!";
                                broadcast_msg(msg,gbaddr,cur_player,false);
                                break;
                        }
                        else if(key == 'j' && gotGold == true && cur_row == gbaddr->rows-1)
                        {
                                mapptr->postNotice("You have Won!!!");
                                string msg = "Player " + playerId + " has Won!!!";
                                broadcast_msg(msg,gbaddr,cur_player,false);
                                break;
                        }
                        else if(key == 'k' && gotGold == true && cur_row == 0)
                        {
                                mapptr->postNotice("You have Won!!!");
                                string msg = "Player " + playerId + " has Won!!!";
                                broadcast_msg(msg,gbaddr,cur_player,false);
                                break;
                        }

                        sem_wait(my_sem_ptr);    

                        if(key == 'h' && cur_col != 0)
                        {
                                draw = true;
                                col = cur_row * gbaddr->columns + cur_col -1;
                                if( gbaddr->board[col] == 0 ||
                                                ((gbaddr->board[col] & G_FOOL) == G_FOOL) ||
                                                ((gbaddr->board[col] & G_GOLD)==G_GOLD)) 
                                {
                                        if(gbaddr->board[col] != G_WALL &&
                                                        ((gbaddr->board[col] & gbaddr->players)==0))
                                        {
                                                if((gbaddr->board[col] & G_FOOL) == G_FOOL)
                                                {
                                                        gotFoolsGold = true;
                                                }
                                                else if((gbaddr->board[col] & G_GOLD) == G_GOLD)
                                                {
                                                        gotGold = true;
                                                        gotGoldPost = true;
                                                }
                                                gbaddr->board[col] |= cur_player;
                                                gbaddr->board[oldIndex] &= ~cur_player;

                                                cur_col = col % gbaddr->columns;
                                                newIndex = col;
                                                oldIndex = col;
                                        }
                                }
                        }
                        //down key
                        else if(key == 'j' && cur_row != gbaddr->rows -1)
                        {
                                draw = true;
                                row = cur_row * gbaddr->columns + cur_col + gbaddr->columns;
                                if( gbaddr->board[row] == 0 ||
                                                ((gbaddr->board[row] & G_FOOL) == G_FOOL)||
                                                ((gbaddr->board[row] & G_GOLD) == G_GOLD))
                                {
                                        if((gbaddr->board[row] != G_WALL) &&((gbaddr->board[row] & gbaddr->players) == 0))
                                        {
                                                if((gbaddr->board[row] & G_FOOL)== G_FOOL)
                                                        gotFoolsGold = true;
                                                else if((gbaddr->board[row] & G_GOLD)== G_GOLD)
                                                {
                                                        gotGoldPost = true;
                                                        gotGold = true;
                                                }  

                                                gbaddr->board[row] |= cur_player;
                                                gbaddr->board[oldIndex] &= ~cur_player;
                                                cur_row = row / gbaddr->columns;
                                                oldIndex = row;
                                                newIndex = row;
                                        }
                                }
                        }

                        else if(key == 'k' && cur_row != 0)
                        {
                                draw = true;
                                row = cur_row * gbaddr->columns + cur_col - gbaddr->columns;
                                if( gbaddr->board[row] == 0 ||
                                                ((gbaddr->board[row] & G_FOOL) == G_FOOL) ||
                                                ((gbaddr->board[row] & G_GOLD)== G_GOLD))
                                {
                                        if(gbaddr->board[row] != G_WALL && ((gbaddr->board[row] & gbaddr->players)==0))
                                        {
                                                if((gbaddr->board[row] & G_FOOL) == G_FOOL)
                                                        gotFoolsGold = true;
                                                else if((gbaddr->board[row] & G_GOLD) == G_GOLD)
                                                {
                                                        gotGoldPost = true;
                                                        gotGold = true;
                                                }            
                                                gbaddr->board[row] |= cur_player;
                                                gbaddr->board[oldIndex] &= ~cur_player;
                                                cur_row = row / gbaddr->columns;
                                                oldIndex = row;
                                                newIndex = row;
                                        }
                                }
                        }
                        //right key
                        else if(key == 'l' && cur_col != gbaddr->columns-1)
                        {
                                draw = true;
                                col = cur_row * gbaddr->columns + cur_col + 1;
                                if( gbaddr->board[col] == 0 ||
                                                ((gbaddr->board[col] & G_FOOL) == G_FOOL) ||
                                                ((gbaddr->board[col] & G_GOLD) == G_GOLD))
                                {
                                        if(gbaddr->board[col] != G_WALL &&((gbaddr->board[col] & gbaddr->players)==0))
                                        {
                                                if((gbaddr->board[col] & G_FOOL) == G_FOOL)
                                                        gotFoolsGold = true;
                                                else if((gbaddr->board[col] & G_GOLD) == G_GOLD)
                                                {
                                                        gotGoldPost = true;
                                                        gotGold = true;
                                                }            
                                                gbaddr->board[col] |= cur_player;
                                                gbaddr->board[oldIndex] &= ~cur_player;
                                                cur_col = col % gbaddr->columns;
                                                oldIndex = col;
                                                newIndex = col;
                                        }
                                }
                        }
                        //send message 
                        else if(key == 'm')
                        {
                                unsigned int player_mask = gbaddr->players;
                                player_mask &= ~cur_player;
                                int p = mapptr->getPlayer(player_mask);
                                int process;
                                if(p != 0)
                                {
                                        switch(p)
                                        {
                                                case G_PLR0: process = 0;
                                                             break;

                                                case G_PLR1: process = 1;
                                                             break;

                                                case G_PLR2: process = 2;
                                                             break;

                                                case G_PLR3: process = 3;
                                                             break;

                                                case G_PLR4: process = 4;
                                                             break;
                                        }
                                        string msg = mapptr->getMessage();
                                        msg = "Player " + get_playerId() + " says: " + msg;
                                        send_msg(process,msg);
                                }
                        }
                        //broadcast msg
                        else if(key == 'b')
                        {  
                                string msg = "";
                                broadcast_msg(msg,gbaddr,true,cur_player);
                        }


                        mapptr->drawMap();
                        if(draw == true)
                        {
                                draw = false;
                                for(int i =0 ; i < 5; i++)
                                {
                                        pid_t pid=getpid();
                                        if(gbaddr->player_process[i] != 0 && gbaddr->player_process[i]!=pid)
                                        {
                                                if(kill(gbaddr->player_process[i],SIGUSR1)==-1)  
                                                        perror("Ouch on kill");
                                        }
                                }
                        }
                        if(gotFoolsGold == true)
                        {
                                mapptr->postNotice("You have got the Fool's Gold!");
                                gotFoolsGold = false;
                        }
                        else if(gotGoldPost == true)
                        {
                                mapptr->postNotice("You have got the Gold!!!");
                                gotGoldPost = false;
                        }
                        sem_post(my_sem_ptr);

                        key = mapptr->getKey();
                }
                sem_wait(my_sem_ptr);
                //When a player exits
                cleanUp();
                sem_post(my_sem_ptr);
                //  delete mapptr;
        }
        catch(int e) {}
}
