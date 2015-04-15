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


struct GameBoard {
   int columns;
    int rows;
    unsigned char players;
    char board[0];
};

typedef struct GameBoard gb; 

using namespace std;
int main()
{
  srand(time(0));

  int numgold = 0;
  char c = ' ';
  int bytes = 0;
  int noLines = 0;
  string line;
  char *len;
  int fd;
  char cur_player = 0;
  gb *gbaddr;
 
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
    
    while(getline(inputMap,line))
    {
        bytes += (unsigned)line.length();
        ++noLines;
    }
    inputMap.clear();
    sem_wait(my_sem_ptr);

    fd = shm_open("/sharedregion_DR", O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH);
    ftruncate(fd, bytes + sizeof(gbaddr));
    /* Map the memory object */
    gbaddr = (gb *) mmap(0, bytes + sizeof(gbaddr), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  
    cur_player = G_PLR0;
    gbaddr->players |= G_PLR0;
    gbaddr->rows = noLines;
    gbaddr->columns = bytes/noLines;

    char* mp = gbaddr->board;

    inputMap.seekg(0);
    inputMap >> numgold;
    inputMap.ignore();

    c = ' ';
    while(inputMap.get(c))
    {
        if(c == ' ')
        {
          *mp = 0;
        }
        else if(c == '*') 
        {
          *mp = G_WALL;
        }
        if(c != '\n')
        {
          ++mp;
        }
    }

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
    gbaddr = (gb *) mmap(0, (r*c) + sizeof(gbaddr), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  
    if((gbaddr->players & G_PLR0) != G_PLR0)
    {
        cur_player = G_PLR0;
    }
    else if((gbaddr->players & G_PLR1) != G_PLR1)
    {   
        cur_player = G_PLR1;
    }
    else if((gbaddr->players & G_PLR2) != G_PLR2)
    {
        cur_player = G_PLR2;
    }
    else if((gbaddr->players & G_PLR3) != G_PLR3)
    {
        cur_player = G_PLR3;
    }
    else if((gbaddr->players & G_PLR4) != G_PLR4)
    {
        cur_player = G_PLR4;
    }
    //if 5 players are already playing the game
    else
    {
        cout << "Game not available now" << endl;
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
  Map goldMine(gbaddr->board,gbaddr->rows,gbaddr->columns);
  sem_post(my_sem_ptr);

  int a=0;
  char key;
  int cur_row = 0;
  int cur_col = 0;
  int col = 0;
  int row = 0;
  int newIndex = 0;
  int oldIndex = 0;

  oldIndex = rval;
  cur_col = rval % gbaddr->columns;   
  cur_row = rval / gbaddr->columns;
  goldMine.postNotice("This is a notice");
  key = goldMine.getKey();
  bool gotGold = false;
  bool gotFoolsGold = false;
  bool gotGoldPost = false;

  while(key!='Q')
  {
     //When player has got the Gold and is leaving the board
     if((cur_row ==0 || cur_row == gbaddr->rows -1 ||
       cur_col ==0 || cur_col == gbaddr->columns-1) && (gotGold == true))
     {
      goldMine.postNotice("You have Won!!!");
      break;
     }
    
    sem_wait(my_sem_ptr);
    //left key
    if(key == 'h' && cur_col != 0)
    {
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
    if(key == 'j' && cur_row != gbaddr->rows -1)
    {
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
    //up key
    if(key == 'k' && cur_row != 0)
    {
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
   if(key == 'l' && cur_col != gbaddr->columns-1)
    {
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
    goldMine.drawMap();
    if(gotFoolsGold == true)
    {
        goldMine.postNotice("You have got the Fool's Gold!");
        gotFoolsGold = false;
    }
    else if(gotGoldPost == true)
    {
        goldMine.postNotice("You have got the Gold!!!");
        gotGoldPost = false;
    }
    sem_post(my_sem_ptr);
    key = goldMine.getKey();
  }
  sem_wait(my_sem_ptr);
  //When a player exits
  gbaddr->players &= ~cur_player;
  gbaddr->board[oldIndex] &= ~cur_player;
  goldMine.drawMap();

  //If all the players have quit the game
  if(gbaddr->players == 0)
  {
    shm_unlink("/sharedregion_DR");
    sem_unlink("/goldChaseSemaphore_DR");
  }
  sem_post(my_sem_ptr);
}
