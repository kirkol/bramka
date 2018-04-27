#include "SDFileSystem.h"
#include "ID12RFID.h"
#include "SC16IS750.h"
#include "EthernetInterface.h"
#include "HTTPClient.h"
#include <string>
#include "Socket.h"
#include "TCPSocketServer.h"
#include "TCPSocketConnection.h"

// Piny SPI dla SC16IS750 - konwertera SPI <-> UART
#define D_MOSI                 p11
#define D_MISO                 p12
#define D_SCLK                 p13
#define D_CS                   p17

Serial pc(USBTX, USBRX); // uzywamy tej linii gdy chcemy nawiazac polaczenie z pc przez USB (uzywanie consoli)
SDFileSystem sd(p5, p6, p7, p9, "sd");            // piny wykorzystywane przez modul czytnika kart
/*Inicjalizacja konwertera SPI na UART*/
SPI spi(D_MOSI, D_MISO, D_SCLK);                  //MOSI, MISO, SCK
SC16IS750_SPI serial_bridge(&spi, D_CS);
/*Inicjalizacja czujnikow RFID*/
ID12RFID rfid_10(p10);                            // uart rx czytnik RFID
ID12RFID rfid_27(p27);                            // uart rx czytnik RFID
ID12RFID rfid_14(p14);                            // uart rx czytnik RFID
DigitalOut Door_YES(p21);                         // Zielona dioda dostepu dla Drzwi nr "1"
DigitalOut Door_NO(p22);                          // Czerwona dioda braku dostepu dla Drzwi nr "1"
DigitalOut Door_YES_1(p25);                       // Zielona dioda dostepu dla Drzwi nr "2"
DigitalOut Door_NO_1(p26);                        // Czerwona dioda braku dostepu dla Drzwi nr "2"
DigitalOut Write_lamp(p8);
DigitalOut Door_open_1(p23);                      // Przekaznik sterujacy elektromagnesem blokujacym Drzwi nr "1" (dla WEJSCIA 1)
DigitalOut Door_open_2(p24);                      // Przekaznik sterujacy elektromagnesem blokujacym Drzwi nr "2" (dla
DigitalOut Door_open_3(p19);                      // Przekaznik sterujacy elektromagnesem blokujacym Drzwi nr "3" (dla
DigitalIn Change_Read_Write(p29);
DigitalOut led1(LED1);                            // use for debug
DigitalOut led2(LED2);                            // use for debug
DigitalOut led3(LED3);                            // use for debug
DigitalOut led4(LED4);                            // use for debug
bool state=0;                               // zmienna informujaca o tym czy przyznano pracownikowi dostep czy nie ...


EthernetInterface eth;
TCPSocketConnection sock;
HTTPClient local;
Ethernet ether;

//jesli zegar sterownika zostal zle ustawiony (sterownik nie mogl polaczyc sie z NTP i ustawil sobie czas domyslny ~1970r)
//sterownik bedzie resetowal sie co 1min, az polaczenie z NTP zostanie przywrocone. Blad sygnalizuje dioda led1=1
void WrongTimeError(){

    led1=1;
    while(1){
        pc.printf("WRONG TIME - RESTART ME WHEN INTERNET IS ON:)   ");
        wait(60);
        NVIC_SystemReset();
    }
}

// funkcja przeniesiona z bliblioteki - okresla czy czytnik ma jeszcze co czytac
int ID12RFID::readable(){
    return _rfid.readable();
}

/*
Czytanie identyfikatora karty bezposrednio przez czytnik RFID

ID12LA     mbed
   READ o
   DI   o
   VCC  o-------------o VCC (5V)
   FORM o-------------o GND
   GND  o-------------o GND
   DO   o-------------o RX
   CP   o
   ANT  o
   ANT  o
   RES  o-------------o VCC
*/
int ID12RFID::read(){

    int v = 0;
    char e = 'A';
    int i = 0;
    char c = 'A';
    int part = 0;

    if(_rfid.readable()){
        while (_rfid.getc()!=("%d",2)){ // jesli kod zacznie sie od znaku innego niz 2, to zwroci v=123 (odczyt zaczynajacy sie na inna niz ASCII=2 to bledny odczyt)
            v = 123;
            pc.printf("NUMBER UNCORRECT");
            while(_rfid.readable()){
             _rfid.getc();
            }
            goto someRubbishDetected;
        }
    }
    v = 0;

    for (i=9; i>=0; i--) {                    // czytanie 10 pierwszych bajtow
        if(_rfid.readable()){
            pc.printf(" BAJT:");
            c = _rfid.getc();                    // czytaj bajt jako char
            pc.printf("%d",c);
            if(c==("%d",2)){                          // jesli karta 2 razy szybko przylozona, to czytnik moze przeczytac 222WlasciweDane i nie zakonczy ich 3 (zablokuje program)
                v = 123;                              // to zabezpieczenie na zbyt szybkie przylozenie 2-krotnie karty
                while(_rfid.readable()){
                    _rfid.getc();
                }
                goto someRubbishDetected;
            }

            part = c - '0';                       // odejmij '0' (czyli 48) - z ASCII "przejscie na decymalny" (widzimy cyfry jakie odpowiadaja ASCII table)
            if(part>=16){                             // part moze osiagnac tez wartosc 10-16 (w ASCII to 65-70)
               part-=7;                               // gdybysmy czytali w HEX i konwertowali na DEC byloby ok - to po prostu szybka konwersja z HEX do DEC
            }

            v |= part << (i * 4);                 // przesuwa o 4*i bity w lewo i robi OR
        }                                         // np. 9*4=36 -> jesli odczytano np. 8, to
                                                  // 1000 0000 0000 0000 0000 0000 0000 0000 0000
    }                                             // kolejne to 8*4=32 -> jesli odczytano np. 3, to
                                                  //      0011 0000 0000 0000 0000 0000 0000 0000
                                                  // po zrobieniu OR wyjdzie:

                                                  // 1000 0011 0000 0000 0000 0000 0000 0000 0000

    while(_rfid.readable()){                      // czytaj (pobieraj znaki) z calego bufora az go wyczyscisz(ile wlezie)
        e = _rfid.getc();
        if(e==("%d",3)){
            pc.printf(" OK ");
        }
        pc.printf(" LAST:%d", e);
    }

    someRubbishDetected:
    pc.printf(" V=%d", v);
    return v;                                     // zwraca nr karty
}

/*SC16IS750:read_from                           */
/*Funkcja czyta id karty poprzez port UART przy */
/*wykorzystaniu konwertera SPI to UART          */
/*
SC16IS750 Breakout o-------o   mbed

   I2C-/SPI  o-------------o GND (wybieramy SPI)
   A0-/CS    o-------------o 17
   A1-SI     o-------------o 11
   NC-S0     o-------------o 12
   SCL-SCK   o-------------o 13
   SDA-VSS   o-------------o GND
   /IRQ      o
   I01       o
   I02       o
   VIN       o
   I03       o
   I04       o
   I05       o
   I06       o
   I07       o
   RTS       o
   CTS       o
   TX        o
   RX        o-------------o ID12LA (czytnik RFID)
   RESET     o
   GND       o-------------o GND
*/
int SC16IS750::read_from(){ // funkcja analogiczna do read() tylko dla WYJSCIA 2 (to sztucznie zrobiony UART)

    int v = 0;
    char e = 'A';
    int i = 0;
    char c = 'A';
    int part = 0;

    if(serial_bridge.readable()){
        while (serial_bridge.getc()!=("%d",2)){ // jesli kod zacznie sie od znaku innego niz 2, to zwroci v=123 (odczyt zaczynajacy sie na inna niz ASCII=2 to bledny odczyt)
            v = 123;
            pc.printf("NUMBER UNCORRECT");
            while(serial_bridge.readable()){
                serial_bridge.getc();
            }
            goto someRubbishDetected2;
        }
    }
    v = 0;

    for (i=9; i>=0; i--) {                    // czytanie 10 pierwszych bajtow
        if(serial_bridge.readable()){
            pc.printf(" BAJT:");
            c = serial_bridge.getc();                    // czytaj bajt jako char
            pc.printf("%d",c);
            if(c==("%d",2)){                          // jesli karta 2 razy szybko przylozona, to czytnik moze przeczytac 222WlasciweDane i nie zakonczy ich 3 (zablokuje program)
                v = 123;                              // to zabezpieczenie na zbyt szybkie przylozenie 2-krotnie karty
                while(serial_bridge.readable()){
                    serial_bridge.getc();
                }
                goto someRubbishDetected2;
            }

            part = c - '0';                       // odejmij '0' (czyli 48) - z ASCII "przejscie na decymalny" (widzimy cyfry jakie odpowiadaja ASCII table)
            if(part>=16){                             // part moze osiagnac tez wartosc 10-16 (w ASCII to 65-70)
               part-=7;                               // gdybysmy czytali w HEX i konwertowali na DEC byloby ok - to po prostu szybka konwersja z HEX do DEC
            }

            v |= part << (i * 4);                 // przesuwa o 4*i bity w lewo i robi OR
        }                                         // np. 9*4=36 -> jesli odczytano np. 8, to
                                                  // 1000 0000 0000 0000 0000 0000 0000 0000 0000
    }                                             // kolejne to 8*4=32 -> jesli odczytano np. 3, to
                                                  //      0011 0000 0000 0000 0000 0000 0000 0000
                                                  // po zrobieniu OR wyjdzie:

                                                  // 1000 0011 0000 0000 0000 0000 0000 0000 0000

    while(serial_bridge.readable()){                      // czytaj (pobieraj znaki) z calego bufora az go wyczyscisz(ile wlezie)
        e = serial_bridge.getc();
        if(e==("%d",3)){
            pc.printf(" OK ");
        }
        pc.printf(" LAST:%d",e);
    }

    someRubbishDetected2:
    pc.printf(" V=$d",v);
    return v;
}


/*WriteToSD                           */
/*Zapis danych na karte SD            */
/*
SparkFun MicroSD Breakout Board
MicroSD Breakout    mbed
   CS  o-------------o 8    (DigitalOut cs)
   DI  o-------------o 5    (SPI mosi)
   VCC o-------------o VOUT
   SCK o-------------o 7    (SPI sclk)
   GND o-------------o GND
   DO  o-------------o 6    (SPI miso)
   CD  o
*/
void WriteToSD(int id_card_number){

    FILE *fp = fopen("/sd/access.txt", "a");  // a+ dopisuje do pliku, w nadpisuje plik
    if(!fp || fp == NULL) {
        NVIC_SystemReset();
        //error("Could not open file for write\n"); // nie uzywam tej funkcji, bo to funkcja blokujaca
    }
    pc.printf("CARD NR WITH PRIVILEGES: ");
    pc.printf("%010d", id_card_number);
    fprintf(fp,"%010d \r\n", id_card_number);       // dopisuje nr aktywowanej karty + enter do pliku access
    fclose(fp);

}
/*ReadFromSD                            */
/*Odczyt danych z karty SD z pliku      */
/*access.txt oraz sprawdzanie id karty  */
/*zeskanowanej z identyfikatorami w pliku*/
void ReadFromSD(int id_card_number, string action, string number){             // id_card_number to zmienna przechowujaca _card wlasnie zeskanowanej

    char temp_buf[11];                           // ...  1 - DOSTEP PRZYZNANO , 0 - BRAK DOSTEPU
    char buf[11];
    sprintf(temp_buf, "%010d", id_card_number);    // konwersja int na tablice char
    string s= temp_buf;                         // konwersja char na string
    int i = 0;                                  
    
    FILE *fp = fopen("/sd/access.txt", "r");    // otwarcie pliku access.txt do odczytu danych (identyfikatorow kart)
    
    if(!fp || fp == NULL) {
        NVIC_SystemReset();
        //error("Could not open file for read\n"); // nie uzywam tej funkcji, bo to funkcja blokujaca
    }
    
    while (!feof(fp)) {
                                 // dopoki nie ma konca pliku
        fgets(buf,11,fp);
                                // pobieranie 11 znakow do bufora buf
        i++;
        // wazne zabezpieczenie. Gdy sterownik nie widzi karty SD po restarcie (uszkodzona, wyjeta, chwilowy brak zasilania na karcie), to jest OK (sterownik sie zresetuje),
        // ALE jesli karta SD zostala wyjeta (lub miala spadek napiecia, ktory ja rozlaczyl) w trakcie dzialania cyklu, program sie zablokuje - bedzie w kolko wykonywal petle while (!feof(fp))
        // (i == 100 000) jest zabezpieczeniem przed tym zapetleniem. Jesli to nastapi, to sterownik sie zrestartuje
        if(i == 100000){                        
            NVIC_SystemReset();
        }
        
        
        if(s == buf) {                          // warunek sprawdzajacy czy zeskanowana karta widnieje w pliku access.txt
            //DOSTEP PRZYZNANO
            if(number == "1") { // gdy drzwi
                Door_YES = 1;
                Door_YES_1 = 1;
                Door_open_1=1;
            }
            if(number == "2"&&action == "wejscie") { // gdy kolowrotek wejscie
                Door_YES = 1;
                Door_YES_1 = 1;
                Door_open_2=1;
            }
            if(number == "2"&&action == "wyjscie") { // gdy kolowrotek wyjscie
                Door_YES = 1;
                Door_YES_1 = 1;
                Door_open_3=1;
            }
            state = 1;
            break;
        }
    }
    if(state == 0) {
        // BRAK DOSTEPU!;
        if(number == "1") {
            Door_NO = 1;
            Door_NO_1 = 1;
        }
        if(number == "2") {
            Door_NO = 1;
            Door_NO_1 = 1;
        }
    }

    fclose(fp);                                 // zamkniecie pliku
    wait(2);

    Door_YES = 0;
    Door_YES_1 = 0;
    Door_NO = 0;
    Door_NO_1 = 0;
    Door_open_1=0;
    Door_open_2=0;
    Door_open_3=0;
}

void send_to_SD(int _id,string action, string number){

    char temporary[9];
    string id_to_string;
    sprintf(temporary, "%d", _id);
    id_to_string = temporary;
    char buf [40];

    time_t czasPrzejscia = time(NULL);
    if ((czasPrzejscia>1490486400&&czasPrzejscia<1509235200)||(czasPrzejscia>1521936000&&czasPrzejscia<1540684800)||(czasPrzejscia>1553990400&&czasPrzejscia<1572134400)||
        (czasPrzejscia>1585440000&&czasPrzejscia<1603584000)||(czasPrzejscia>1616889600&&czasPrzejscia<1635638400)||(czasPrzejscia>1648339200&&czasPrzejscia<1667088000)||
        (czasPrzejscia>1679788800&&czasPrzejscia<1698537600)||(czasPrzejscia>1711843200&&czasPrzejscia<1729987200)||(czasPrzejscia>1743292800&&czasPrzejscia<1761436800)||
        (czasPrzejscia>1774742400&&czasPrzejscia<1792886400)||(czasPrzejscia>1806192000&&czasPrzejscia<1824940800)||(czasPrzejscia>1837641600&&czasPrzejscia<1856390400)||
        (czasPrzejscia>1869091200&&czasPrzejscia<1887840000)||(czasPrzejscia>1901145600&&czasPrzejscia<1919289600)){
        czasPrzejscia = time(NULL)+7200; // dodanie 2 godzin do czasu z serwera NTP
    }else{
        czasPrzejscia = time(NULL)+3600; // dodanie 1 godziny do czasu z serwera NTP
    }
    strftime(buf,40,"%Y-%m-%d %H:%M:%S",localtime(&czasPrzejscia)); // ustawienie formatu daty

    string linia = "";
    linia = "";
    linia.append(id_to_string);
    linia+= "$";
    linia.append(action);
    linia+= "$";
    linia.append(buf);
    linia+= "$";

    pc.printf("%s",linia);

    FILE *fp2 = fopen("/sd/buffor.txt", "a");  // dodaj linie do pliku buffor.txt (przechowuje informacje wejsc/wyjsc, gdy nie ma polaczenia z siecia)
    if(!fp2 || fp2 == NULL) {
        NVIC_SystemReset();
        //error("Could not open file for write\n"); // nie uzywam tej funkcji, bo to funkcja blokujaca
    }
    fprintf(fp2,"%s\r\n",linia.c_str());       // sprawdz czy nie trzeba pisac znaku nowej linii

    pc.printf("LINE WENT TO SD ");

    fclose(fp2);
    pc.printf("%s",linia.c_str());
}

/*send_to_server                */
/*Wysylanie danych na serwer    */
/*Wysylane dane to id karty     */
/*zeskanowanej oraz dzialanie   */
/*"wejscie" albo "wyjscie"      */
/*
Ethernet Breakout    mbed
   P1  o-------------o RD-
   P2  o-------------o TD-
   P3  o-------------o RD+
   P4  o
   P5  o
   P6  o-------------o TD+
   P7  o
   P8  o
   Y-  o
   Y+  o
   G-  o
   G+  o
*/

void send_to_server(int _id,string action, string number){

    char temporary[9];
    string request="";
    string id_to_string;
    int conn=-1; // zmienna ustalajaca czy jest polaczenie z socketem
    char http_cmd[300];

    sprintf(temporary, "%d", _id);
    id_to_string = temporary;
    conn = sock.connect("192.168.90.123", 80);
    pc.printf("%d",conn);
    if(conn==0){
        pc.printf("SERVER OK \r\n");

        request = "GET /create_MariaDB.php?id_card=";
        request.append(id_to_string);
        request+= "&action=";
        request.append(action);
        request+= " HTTP/1.0\r\n\r\n";

        pc.printf("%s\r\n", request);

        strcpy(http_cmd, request.c_str());
        sock.send_all(http_cmd, sizeof(http_cmd)-1);
        pc.printf("SENDED TO SERVER\r\n");
        sock.close();
    }else{
        pc.printf("SERVER OFF\r\n");
        send_to_SD(_id, action, number);
        pc.printf("SENDED TO SD\r\n");
    }
}

// send_to_DB funkcja zrzucajaca plik buffor.txt do bazy i nadpisujaca plik buffor.txt pustym plikiem po wykonaniu zrzutu

void send_to_DB(){

    char x='a';
    int conn=-1;                                   // zmienna ustalajaca czy jest polaczenie z netem
    string buf4id="";                              // string przeznaczony na nr karty
    string buf4action="";                          // string przeznaczony na typ wejscia/wyjscia
    string buf4time="";                            // string przeznaczony na czas - dzien i godzina
    string request="";                         // string przeznaczony na request HTTP
    int i = 0;

    FILE *fp = fopen("/sd/buffor.txt", "r");    // otwarcie pliku access.txt do odczytu danych
    if(!fp || fp == NULL){                      // jezeli cos nie tak z karta SD (jest wyjeta, uszkodzona), to zrob reset
        NVIC_SystemReset();
        //error("Could not open file for write\n"); // nie uzywam tej funkcji, bo to funkcja blokujaca
    }

    while (!feof(fp)) {                             //czytaj dopoki nie ma konca pliku

        buf4id="";
        buf4action="";
        buf4time="";
        
        i++;
        // wazne zabezpieczenie. Gdy sterownik nie widzi karty SD po restarcie (uszkodzona, wyjeta, chwilowy brak zasilania na karcie), to jest OK (sterownik sie zresetuje),
        // ALE jesli karta SD zostala wyjeta (lub miala spadek napiecia, ktory ja rozlaczyl) w trakcie dzialania cyklu, program sie zablokuje - bedzie w kolko wykonywal petle while (!feof(fp))
        // (i == 100 000) jest zabezpieczeniem przed tym zapetleniem. Jesli to nastapi, to sterownik sie zrestartuje
        if(i == 100000){                        
            NVIC_SystemReset();
        }

        x='a';
        while(x!='$'){
            x=fgetc(fp);
            pc.printf("%d", x);
            pc.printf("\n");
            if(x==("%d",255)){                      // przy ostatniej linii pojawia sie ASCII=255 (non-breaking space) - ma ja pominac
                pc.printf(" EOF ");
                break;
            }
            if(x!='$'){
                buf4id=buf4id+x;
            }
        }

        x='a';
        while(x!='$'){
            x=fgetc(fp);                            // pobierz jeden znak
            pc.printf("%d", x);
            pc.printf("\n");
            if(x==("%d",255)){
                pc.printf(" EOF ");
                break;
            }
            if(x!='$'){
                buf4action=buf4action+x;
            }
        }

        x='a';
        while(x!='$'){
            x=fgetc(fp);                            // pobierz jeden znak
            pc.printf("%d", x);
            pc.printf("\n");
            if(x==("%d",255)){
                pc.printf(" EOF ");
                break;
            }
            if(x==' '){ // podmiana spacji na symbol H (umozliwia wyslanie zapytania przez HTTP. HTTP nie przyjmuje spacji)
                x='H';
            }
            if(x!='$'){
                buf4time=buf4time+x;
            }
        }

        string buf4idstring = buf4id.c_str();
        string buf4actionstring = buf4action.c_str();
        string buf4timestring = buf4time.c_str();

        pc.printf("%s", buf4idstring);
        pc.printf("%s", buf4actionstring);
        pc.printf("%s", buf4timestring);

        x=fgetc(fp); // czyta decymalne "13" (carriege return) zeby nie dostalo sie do zapytania HTTP
        pc.printf("%d", x);
        x=fgetc(fp); // czyta decymalne "10" (line feed) zeby nie dostalo sie do zapytania HTTP
        pc.printf("%d", x);

        //wyslij linie z pliku za pomoca HTTP
        if(buf4idstring!=""){
            conn = sock.connect("192.168.90.123", 80);
            if(conn==0){
                pc.printf("jest Socket W SEND TO DB\r\n");
                request = "GET /MariaDB_send_from_buffor_to_DB.php?id_card=";
                request.append(buf4idstring);
                request+= "&action=";
                request.append(buf4actionstring);
                request+= "&data=";
                request.append(buf4timestring);
                request+= " HTTP/1.0\r\n\r\n";
                pc.printf("%s", request);

                char http_cmd[500];
                strcpy(http_cmd, request.c_str());
                sock.send_all(http_cmd, sizeof(http_cmd)-1);
                wait(0.1);
            }else{
                pc.printf("SERVER OFF\r\n");
            }
            sock.close();
        }
    }

    fclose(fp); // zamyka plik do odczytu

    //jesli jest polaczenie z siecia i z socketem, to nadpisz plik buffor plikiem pustym, jezeli nie, to plik zostanie na karcie
    if(ether.link()==true&&conn==0){
        pc.printf("CLEAN BUFFOR\r\n");
        FILE *fp = fopen("/sd/buffor.txt", "w");    // nadpisanie pliku buffor.txt pustym plikiem buffor.txt
        if(!fp || fp == NULL) {
            NVIC_SystemReset();
            //error("Could not open file for read\r\n"); // nie uzywam tej funkcji, bo to funkcja blokujaca
        }
        pc.printf("OVERWRITED\r\n");
        NVIC_SystemReset();
    }
    fclose(fp); // zamyka plik do "nadpisu"
}
