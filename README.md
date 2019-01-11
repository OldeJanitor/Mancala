# Mancala
A school assignment done for CSC209 - Software Tools and Systems Programming. This is a program that runs a local server which hosts a commandline game of Mancala in which players can freely join and dropout without interrupting the game :)

## How to Play :)

You will need to run the executable 'manscrv -p 30000' on a commandline program such as Bash (Ubuntu command line for Windows), this will start the Mancala Server on port 30000. To add players and play the game, in another commandline window you can connect to the server by using a Netcat command like so...
  
    nc localhost 30000
 
After this the game will prompt you to insert your player name and it will notify you when it is your turn. To make a move, simply input a digit from 0-6 which will be the pit you select to move pebbles from. Have Fun!

If you are unaware of the rules of Mancala watch this video :)
https://www.youtube.com/watch?v=b5UiPrjlPqM
However, note that this version can have many more than 2 players!

### Prerequisites

* Commandline Program - I used Bash :)
* gcc - if you wish to compile the code yourself
* nc - access to the Netcat commandline function


## Built With

* C

## Authors

* **Nikolas Till**
