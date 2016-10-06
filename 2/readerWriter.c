/*
 * Soubor:  readerWriter.c
 * Datum:   2012/05/02
 * Autor:   Martin Borek, xborek08@stud.fit.vutbr.cz
 * Projekt: Druhy projekt pro predmet IOS
 * Popis:   Program implementuje synchronizacni problem ctenar - pisar
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>

/** Hodnoty chyb programu */
enum tecodes
{
  EOK = 0,     /**< Bez chyby */
  EPARAM,    /**< Chybny prikazovy radek */
  EPAMET,
  ESEMAFOR,   
  ESEMCLOSE, 
  ESEMUNLINK,
  ESHMUNLINK,
  ESOUBOR,   /**< Chyba pri otevirani souboru pro zapis */
  EFORK,
  EUNKNOWN,    /**< Neznama chyba */
};

/**
 * Stavove kody programu.
 */
enum tstates
{
  CRAZENI,        /**< Razeni seznamu */
  CHELP,       /**< Napoveda */
};

/** Chybova hlaseni odpovidajici hodnotam chyb. */
const char *ECODEMSG[] =
{
  [EOK] = "Vse v poradku.\n",
  [EPARAM] = "Nespravne zadane parametry!\n",
  [EPAMET] = "Nepodarilo se vytvorit sdilenou pamet\n",
  [ESEMAFOR] = "Nepodarilo se vytvorit semafor!\n",
  [ESEMCLOSE] = "Nepodarilo se zavrit semafor!\n",
  [ESEMUNLINK] = "Nepodarilo se unlinkovat semafor!\n",
  [ESHMUNLINK] = "Nepodarilo se unlinkovat sdilenou pamet!\n",
  [ESOUBOR] = "Nepodarilo se otevrit soubor pro zapis!\n",
  [EFORK] = "Chyba pri vytvareni potomka\n",
  [EUNKNOWN] = "Nastala nepredvidana chyba!\n",
};

/** Struktura obsahujici hodnoty parametru prikazove radky. */
typedef struct params
{
  int ecode;
  int pocetPisaru;
  int pocetCtenaru;
  unsigned pocetCyklu;
  unsigned pisarSleep;
  unsigned ctenarSleep;
  FILE * vystup;
} tParams;


/** Struktura pouzita pri mapovani sdilene pameti. */
typedef struct sdilene
{
  int prostor;
  unsigned int citac;
  int ctenari;
  int pisari;
} tSdilene;

int errorStatus = 0; // globalni promenna pro nastaveni chyb
tSdilene *sdilene; // sdilena pamet
/** semafory */
sem_t *semexkl, *semctenarexkl, *sempisarexkl, *semctenar, *sempisar, *semvypis;

/**
 * Vytiskne hlaseni odpovidajici hodnote chyby.
 * @param ecode Hodnota chyby programu.
 */
void printError(int ecode)
{
  if (ecode < EOK || ecode > EUNKNOWN)
    ecode = EUNKNOWN;

  fprintf(stderr, "%s", ECODEMSG[ecode]);
  errorStatus = 2;
}
/** Mazani semaforu a sdilene pameti,... pocet zalezi na parametru "kolik" */
void ukonceni(int kolik)
{
  if ((munmap(sdilene, sizeof(tSdilene))) == -1)
    printError(ESHMUNLINK);

  if (shm_unlink("/xborek08sdilene") == -1)
    printError(ESHMUNLINK);

  if (kolik >= 1){
    if (sem_close(semexkl) == -1)
      printError(ESEMCLOSE);
    if (sem_unlink("/xborek08exkl") == -1)
      printError(ESEMUNLINK);
  }
  if (kolik >= 2){
    if (sem_close(semctenarexkl) == -1)
      printError(ESEMCLOSE);
    if (sem_unlink("/xborek08ctenarexkl") == -1)
      printError(ESEMUNLINK);
  }
  if (kolik >= 3){
    if (sem_close(sempisarexkl) == -1)
      printError(ESEMCLOSE);
    if (sem_unlink("/xborek08pisarexkl") == -1)
      printError(ESEMUNLINK);
  }
  if (kolik >= 4){
    if (sem_close(sempisar) == -1)
      printError(ESEMCLOSE);
    if (sem_unlink("/xborek08pisar") == -1)
      printError(ESEMUNLINK);
  }
  if (kolik >= 5){
    if (sem_close(semctenar) == -1)
      printError(ESEMCLOSE);
    if (sem_unlink("/xborek08ctenar") == -1)
      printError(ESEMUNLINK);
  }
  if (kolik >= 6){
    if (sem_close(semvypis) == -1)
      printError(ESEMCLOSE);
    if (sem_unlink("/xborek08vypis") == -1)
      printError(ESEMUNLINK);
  }
  exit(errorStatus);
}

/**
 * Zpracuje parametry prikazoveho radku a vrati je ve strukture tParams.
 * Je-li format parametru chybny, vrati hodnotu chyby.
 * @param argc Pocet argumentu.
 * @param argv Pole textovych retezcu s argumenty.
 * @return Vraci analyzovane argumenty prikazoveho radku.
 */
tParams getParams(int argc, char *argv[])
{
  tParams result = 
  { // inicializace struktury na defaultni hodnoty
    .ecode = EOK,
    .vystup = stdout,
  };

  char * retezec;
  
  if (argc != 7)
  { // spatny pocet parametru
    result.ecode = EPARAM; 
  }
  else
  {
    if (isdigit(*(argv[1])))
    {
      result.pocetPisaru = strtoul(argv[1], &retezec, 10);
      if (*retezec != '\0')
        result.ecode = EPARAM;
    }
    else
      result.ecode = EPARAM;
    
    if (isdigit(*(argv[2])))
    {
      result.pocetCtenaru = strtoul(argv[2], &retezec, 10);
      if (*retezec != '\0')
        result.ecode = EPARAM;
    }
    else
      result.ecode = EPARAM;
    
    if (isdigit(*(argv[3])))
    {
      result.pocetCyklu = strtoul(argv[3], &retezec, 10);
      if (*retezec != '\0')
        result.ecode = EPARAM;
    }
    else
      result.ecode = EPARAM;

    if (isdigit(*(argv[4])))
    {
      result.pisarSleep = strtoul(argv[4], &retezec, 10);
      if (*retezec != '\0')
        result.ecode = EPARAM;
    }
    else
      result.ecode = EPARAM;
    
    if (isdigit(*(argv[5])))
    {
      result.ctenarSleep = strtoul(argv[5], &retezec, 10);
      if (*retezec != '\0')
        result.ecode = EPARAM;
    }
    else
      result.ecode = EPARAM;
    if (argv[6][0] != '-')
      if ((result.vystup = fopen(argv[6], "w")) == NULL)
        result.ecode = ESOUBOR;
  }
  return result;
}

/** Proces ctenare. */
void ctenar(int cisloCtenare, tParams *params)
{
  int prectenaHodnota;
  do
  { // opakovani, dokud se neprovede pocet cyklu
    sem_wait(semvypis);
    fprintf(params->vystup, "%u: reader: %i: ready\n", (sdilene->citac)++, cisloCtenare);
    sem_post(semvypis);

    /** Nastavovani semaforu */
    sem_wait(semexkl);
    sem_wait(semctenar);
    sem_wait(semctenarexkl);
    sdilene->ctenari++;
    if (sdilene->ctenari == 1)
      sem_wait(sempisar);
    sem_post(semctenarexkl);
    sem_post(semctenar);
    sem_post(semexkl);
    
    /** Cteni - zacatek */
    sem_wait(semvypis);
    fprintf(params->vystup, "%u: reader: %i: reads a value\n", (sdilene->citac)++, cisloCtenare);
    sem_post(semvypis);

    prectenaHodnota = sdilene->prostor;
    
    sem_wait(semvypis);
    fprintf(params->vystup, "%u: reader: %i: read: %i\n", (sdilene->citac)++, cisloCtenare, prectenaHodnota);
    sem_post(semvypis);
    /** Cteni - konec */

    sem_wait(semctenarexkl);
    sdilene->ctenari--;
    if (sdilene->ctenari == 0)
      sem_post(sempisar);
    sem_post(semctenarexkl);
 
    if (params->ctenarSleep > 0)
      usleep((rand() % params->ctenarSleep) * 1000);
  } while(prectenaHodnota != 0);

  munmap(sdilene, sizeof(tSdilene));
  exit(EXIT_SUCCESS);
}

/** Proces pisare. */
void pisar(int cisloPisare, tParams *params)
{
  unsigned i;
  for (i = 0; i < params->pocetCyklu; i++)
  {
    sem_wait(semvypis);
    fprintf(params->vystup, "%u: writer: %i: new value\n", (sdilene->citac)++, cisloPisare);
    sem_post(semvypis);

    if (params->pisarSleep > 0)
      usleep((rand() % params->pisarSleep) * 1000);

    sem_wait(semvypis);
    fprintf(params->vystup, "%u: writer: %i: ready\n", (sdilene->citac)++, cisloPisare);
    sem_post(semvypis);
   
    /** Nastavovani semaforu */
    sem_wait(sempisarexkl);
    sdilene->pisari++;
    if (sdilene->pisari == 1)
      sem_wait(semctenar);
    sem_post(sempisarexkl);
    sem_wait(sempisar);

    /** Zapisovani - zacatek */
    sem_wait(semvypis);
    fprintf(params->vystup, "%u: writer: %i: writes a value\n", (sdilene->citac)++, cisloPisare);
    sem_post(semvypis);

    sdilene->prostor = cisloPisare;
    
    sem_wait(semvypis);
    fprintf(params->vystup, "%u: writer: %i: written\n", (sdilene->citac)++, cisloPisare);
    sem_post(semvypis);

    /** Zapisovani - konec */
    
    sem_post(sempisar);

    sem_wait(sempisarexkl);
    sdilene->pisari--;
    if (sdilene->pisari == 0)
      sem_post(semctenar);
    sem_post(sempisarexkl);
  }
  munmap(sdilene, sizeof(tSdilene));
  exit(EXIT_SUCCESS);

}

/** Hlavni program. */
int main(int argc, char *argv[])
{

  setbuf(stdin, NULL);  // aby se polozky vypisovaly ve spravnem poradi
  tParams params = getParams(argc, argv);
  if (params.ecode != EOK)  // chyba pri nacitani parametru
  {
    printError(params.ecode);
    exit(1);
  }

  srand(time(NULL));
  setbuf(params.vystup, NULL);

 /** Sdilena pamet - zacatek */
  int fd = shm_open("/xborek08sdilene", O_RDWR|O_CREAT|O_EXCL, 0666);
  if (fd < 0)
  {
    printError(EPAMET);
    exit(2);
  }
  if (ftruncate(fd, sizeof(tSdilene)))
  {
    printError(EPAMET);
    close(fd);
    ukonceni(0);
    exit(2);
  }
  sdilene = mmap(NULL, sizeof(tSdilene), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (sdilene == MAP_FAILED)
  {
    printError(EPAMET);
    ukonceni(0);
    exit(2);
  }
  sdilene->prostor = -1;
  sdilene->citac = 1;

 /** Sdilena pamet - konec */
 /** Semafory - zacatek */

  semexkl = sem_open("/xborek08exkl", O_CREAT|O_EXCL, 0666, 1);
  if (semexkl == SEM_FAILED)
  {
    printError(ESEMAFOR);
    ukonceni(0);
  }
  semctenarexkl = sem_open("/xborek08ctenarexkl", O_CREAT|O_EXCL, 0666, 1);
  if (semctenarexkl == SEM_FAILED)
  {
    printError(ESEMAFOR);
    ukonceni(1);
  }
  sempisarexkl = sem_open("/xborek08pisarexkl", O_CREAT|O_EXCL, 0666, 1);
  if (sempisarexkl == SEM_FAILED)
  {
    printError(ESEMAFOR);
    ukonceni(2);
  }
  sempisar = sem_open("/xborek08pisar", O_CREAT|O_EXCL, 0666, 1);
  if (sempisar == SEM_FAILED)
  {
    printError(ESEMAFOR);
    ukonceni(3);
  }
  semctenar = sem_open("/xborek08ctenar", O_CREAT|O_EXCL, 0666, 1);
  if (semctenar == SEM_FAILED)
  {
    printError(ESEMAFOR);
    ukonceni(4);
  }
  semvypis = sem_open("/xborek08vypis", O_CREAT|O_EXCL, 0666, 1);
  if (semvypis == SEM_FAILED)
  {
    printError(ESEMAFOR);
    ukonceni(6);
  }

 /** Semafory - konec */

  int i;
  pid_t pid;
  pid_t pisari[params.pocetPisaru];
  pid_t ctenari[params.pocetCtenaru];
  for (i = 1; i <= params.pocetPisaru; i++)
  { //vytvareni pisaru
    pid = fork();
    if (pid == 0)
    { // novy potomek
      pisar(i, &params);
    }
    else if(pid < 0)
    { // nastala chyba, musi se zabit potomci
ukonceni(5);

      for (int l = 0; l < i - 1; l++)
        kill(pisari[l], SIGKILL);

      printError(EFORK);
      exit(2);
    }
    else
    { // rodic
      pisari[i-1] = pid;
    }
  }
  int abcde =0;
  for (i = 1; i <= params.pocetCtenaru; i++)
  { //vytvareni ctenaru
  abcde++;
    if (abcde != 4)
      pid = fork();
    else
      pid = -1;
    if (pid == 0)
    { // novy potomek
      ctenar(i, &params);
    }
    else if(pid < 0)
    { // nastala chyba, musi se zabit potomci
      ukonceni(6);
      for (int l = 0; l < i - 1; l++)
      {
        kill(ctenari[l], SIGKILL);
	}

      for (int l = 0; l < params.pocetPisaru; l++)
      {
        kill(pisari[l], SIGKILL);
}
      printError(EFORK);
      exit(2);
    }
    else
    { // rodic
      ctenari[i-1] = pid;
    }
  }
  
  for (i = 1; i <= params.pocetPisaru; i++)
    waitpid(pisari[i], NULL, 0);
  
  /** Zapis nuly - zacatek */
  sem_wait(semctenar);
  sem_wait(sempisar);

  /** Zapisovani - zacatek */
  sdilene->prostor = 0;
  /** Zapisovani - konec */
    
  sem_post(sempisar);
  sem_post(semctenar);
  /** Zapis nuly - konec */
  
  for (i = 1; i <= params.pocetCtenaru; i++)
    waitpid(ctenari[i], NULL, 0);


  ukonceni(6); // smazani semaforu a sdilene pameti
 
  if (params.vystup != stdin)
    fclose(params.vystup);
 
  return errorStatus;
}
