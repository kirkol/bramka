#include "mbed.h"
#include "function.h"
#include "NTPClient.h"
#include "EthernetInterface.h"
#include "HTTPClient.h"
#include <iostream>;

int _id_card=0;                                     // zmienna przechowujaca numer karty
bool internetA = 0;									// flaga polaczenia z internetem (sprawdza czy zaszly zmiany w polaczeniu z off na on)
bool internetB = 0;									// flaga polaczenia z internetem (sprawdza czy zaszly zmiany w polaczeniu z off na on)
int sockConnA = -1;									// flaga polaczenia z serverem (sprawdza czy zaszly zmiany w polaczeniu z off na on)
int sockConnB = -1;									// flaga polaczenia z serverem (sprawdza czy zaszly zmiany w polaczeniu z off na on)
int conn = -1;										// flaga polaczenia z serwerem
bool err = false;									// flaga poprawnego czasu (jesli czas OK, to false, jesli sterownik nie polaczy sie z NTP, to ustawia true i co minute robi reset)
float aproxCycleTime = 0.2; 						// zmienna ustawiajaca ile ma trwac jedno przejscie cyklu (~jedno mrugniecie diody LED3)
int cyclesIn30sec = 30/aproxCycleTime;				// obliczenie ile cykli wykonuje sie w ciagu 30 sek
int i = 0;											// nr cyklu (zeruje sie co 30sek)
time_t time_in_sec = 0;								// czas sterownika w sekundach (uzywane do robienia resetu 1/dzien)
char buffer[40];									// bufor dla daty i czasu sterownika
char hour[10];										// bufor dla godziny sterownika
bool state_Read_Write = 0;							// flaga stanu czytnika


int main(){

    pc.printf("Starting... \r\n");

    // stworz polaczenie z netem, polacz, ustaw czas
    eth.init("192.168.90.247", "255.255.255.0", "192.168.90.254");
    eth.connect();

    if (ntp.setTime("0.pl.pool.ntp.org") == 0){
        pc.printf("Set time successfully\r\n");
        time_t ctTime = time(NULL);
        strftime(buffer,40,"%Y-%m-%d %H:%M:%S",localtime(&ctTime)); // ustawienie formatu daty
        pc.printf("%s\r\n",buffer);
        if (ctTime<946708560){ // jezeli czas jest mniejszy niz rok 2000 (czyli ustawilo czas np. na 1970 rok - czas domyslny. Ustawia sie, gdy sterownik nie mogl nawiazac polaczenia z NTP)
            WrongTimeError();
        }
    }
    else{
      pc.printf("Error\r\n");
      WrongTimeError();
    }
    eth.disconnect();

    eth.connect();
    // po wlaczeniu sterownika nastapi zrzut z buffora SD, jesli jest polaczenie z internetem. Plik buffora nie zostanie skasowany, jesli nie ma polaczenia z serwerem
    if(ether.link()){
        pc.printf("SEND TO DB IS ON!!!\r\n");
        send_to_DB();
    }

    eth.connect();
//
//
//
    // ROZPOCZECIE PETLI
    while(1){

        led2=1; // pokaz czy w ogole weszlo w petle

        // mrugniecie diody led3 -> pokazanie czy sterownik zyje (czy cykl sie wykonuje)
        led3=1;
        wait(aproxCycleTime);
        led3=0;

        //raz dziennie rob reset w nocy
        time_in_sec = time(NULL);
        strftime(hour,10,"%H:%M:%S",localtime(&time_in_sec));
        if(strstr(hour, "21:30:0") != NULL){ // sterownik ma ustawiony czas z NTP, czyli ZAWSZE czas UTC (-1h lub -2h w strosunku do czasu polskiego)
            pc.printf("I reset myself \r\n");
            NVIC_SystemReset();
        }

        // sprawdz stan polaczenia z internetem (czy przewod lub switch sa OK). Ta funkcja NIE sprawdza polaczenia z serwerem
        // jezeli nie bylo polaczenia, a zostalo wznowione, to sprobuje polaczyc sie z serwerem i zrzucic dane z buffora
        internetA = ether.link();
        if(internetA == 1 && internetB == 0){
        	conn = sock.connect("192.168.90.123", 80);
        	if(conn==0){
        		sock.close();
        		pc.printf("SEND TO DB IS ON!!!\r\n");
        		send_to_DB();
        	}
        	sock.close();
        }
        internetB = internetA;
        if(internetA == 0){
        	pc.printf("NET OFF\r\n");
        	led1 = 1; wait(0.1); led1 = 0;
        }

        //raz na pol minuty polacz z serwerem (sprawdza stan polaczenia z serwerem)
        //jezeli nie bylo polaczenia, a zostalo wznowione, to sprobuje zrzucic dane z buffora
        if(i == cyclesIn30sec){
            i=0;
            if(ether.link()){
                pc.printf("CONNECTING TO SERVER\r\n");
                sockConnA = sock.connect("192.168.90.123", 80);
                if(sockConnA==0){
                    pc.printf("SERVER OK\r\n");
                }else{
                    pc.printf("SERVER OFF\r\n");
                }
                sock.close();
            }else{
                pc.printf("NET OFF\r\n");
                led1 = 1; wait(0.1); led1 = 0;
            }
        }
        if(sockConnA == 0 && sockConnB != 0){
    		pc.printf("SEND TO DB IS ON!!!\r\n");
    		send_to_DB();
    		sock.close();
        }
        sockConnB = sockConnA;
        if(sockConnA != 0){
        	pc.printf("SERV_STATE=%d", sockConnA); // jezeli cos nie tak z serwerem, to printuj nr bledu
        	led4 = 1; wait(0.1); led4 = 0;
        }
        i++;

        //sprawdzenie czy jest wcisniety przycisk zapisu (zapis nr karty na SD - te karty beda otwierac drzwi)
        if(Change_Read_Write == 1){
            led2 = 0; wait(0.2);
            state_Read_Write = !state_Read_Write;
            while(Change_Read_Write == 1){
                if(state_Read_Write == 1){
                    Write_lamp = 1;
                }else{
                    Write_lamp = 0;
                }
                wait(0.2);
            }
        }
        else{
            if(state_Read_Write == 1) {
                    Write_lamp = 1;
            }else{
                    Write_lamp = 0;
                }
            wait(0.2);
        }

        // odczyt z karty WEJSCIE 1 (przy drzwiach)
            if(rfid_10.readable()){                // WEJSCIE 1 warunek sprawdzajacy czy do czujnika zostala przylozona karta
                _id_card = rfid_10.read();          // odczyt numeru karty
                led4=1; wait(0.2); led4=0;
                if(state_Read_Write == 0) {
                    printf("RFID NUMBER : %d\r\n", _id_card);
                    if(_id_card!=123){                                    // jesli funkcja read() zwrocila 123, to czytnik przeczytal jakies bzdury
                        ReadFromSD(_id_card,"wejscie","1");               // funkcja sprawdzajaca numer karty w pliku access.txt
                        if(ether.link()){                                 //sprawdz czy jest internet, jesli jest to wyslij na serwer, jesli nie to na SD do pliku buffer
                            send_to_server(_id_card,"wejscie","1");       // wysylanie odczytanych informacji na serwer do bazy danych FAT.sqlite
                            pc.printf("SENDED TO SERVER!!!");
                        }else{
                            send_to_SD(_id_card,"wejscie","1");
                            pc.printf("SENDED TO SD!!!");
                        }
                    }else{
                        pc.printf("rubbish detected");
                    }
                    wait(0.5);
                }
                // czytnik WEJSCIE 1 jest w trybie zapisu
                if(state_Read_Write == 1) {
                    if(_id_card!=123){
                        if(ether.link()){ //sprawdz czy jest internet, jesli jest to wyslij na serwer, jesli nie to zolta dioda zasygnalizuj blad (trzy razy mrugnij)
                            WriteToSD(_id_card);
                            send_to_server(_id_card,"nowy_pracownik","1");
                        }else{
                            Write_lamp = 0; wait(0.3); Write_lamp = 1; wait(0.3); Write_lamp = 0; wait(0.3); Write_lamp = 1; wait(0.3); Write_lamp = 0; wait(0.3);
                        }
                    }else{
                        pc.printf(" rubbish detected ");
                    }
                    Write_lamp = 1;
                }else{
                	Write_lamp = 0;
                }
            }

        // odczyt z karty WYJSCIE 1 (przy drzwiach)
            if(rfid_27.readable()) { // WYJSCIE 1
                _id_card =rfid_27.read();
                led4=1; wait(0.2); led4=0;
                printf("RFID NUMBER : %d\n", _id_card );
                if(_id_card!=123){
                    ReadFromSD(_id_card,"wyjscie","1");
                    if(ether.link()){ //sprawdz czy jest internet, jesli jest to wyslij na serwer, jesli nie to na SD do pliku buffer
                        send_to_server(_id_card,"wyjscie","1"); // wysylanie odczytanych informacji na serwer do bazy danych FAT.sqlite
                        pc.printf("SENDED TO SERVER!!!");
                    }else{
                        send_to_SD(_id_card,"wyjscie","1");
                        pc.printf("SENDED TO SD!!!");
                    }
                }else{
                    pc.printf(" rubbish detected ");
                }
                wait(0.5);
            }

       // odczyt z karty WEJSCIE 2 (przy kolowrotku)
            if(rfid_14.readable()) {   // WEJSCIE 2
                _id_card =rfid_14.read();
                led4=1; wait(0.2); led4=0;
                printf("RFID NUMBER : %d\n", _id_card );
                if(_id_card!=123){
                    ReadFromSD(_id_card,"wejscie","2");
                    if(ether.link()){ //sprawdz czy jest internet, jesli jest to wyslij na serwer, jesli nie to na SD do pliku buffer
                        send_to_server(_id_card,"wejscie","2"); // wysylanie odczytanych informacji na serwer do bazy danych FAT.sqlite
                        pc.printf("SENDED TO SERVER!!!");
                    }else{
                        send_to_SD(_id_card,"wejscie","2");
                        pc.printf("SENDED TO SD!!!");
                    }
                }else{
                    pc.printf(" rubbish detected ");
                }
                wait(0.5);
            }

        // odczyt z karty WYJSCIE 2 (przy kolowrotku)
            if(serial_bridge.readable()) {  //WYJSCIE 2
                led4=1; wait(0.2); led4=0;
                _id_card = serial_bridge.read_from();
                printf("RFID NUMBER : %d\n", _id_card );
                if(_id_card!=123){
                    ReadFromSD(_id_card,"wyjscie","2");
                    if(ether.link()){ //sprawdz czy jest internet, jesli jest to wyslij na serwer, jesli nie to na SD do pliku buffer
                        send_to_server(_id_card,"wyjscie","2"); // wysylanie odczytanych informacji na serwer do bazy danych FAT.sqlite
                        pc.printf("SENDED TO SERVER!!!");
                    }else{
                        send_to_SD(_id_card,"wyjscie","2");
                        pc.printf("SENDED TO SD!!!");
                    }
                }else{
                    pc.printf(" rubbish detected ");
                }
                wait(0.5);
            }
        }
}
