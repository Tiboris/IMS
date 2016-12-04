#include "simlib.h"

#include <limits>	// pre std::numeric_limits<int>::max()
#include <iostream>	// std::cerr
#include <cmath>	// std::abs
#include <vector>

// 1 pracovny den v minutach - 10 hodin
#define DAY 600

// prichody zakaznikov 0.78min = 46.8s
// 0.85
#define ARRIVAL 0.85
// prichod dochodcov pre dochodky
#define PENSION 5

// sluzby
#define LIST 1
#define POD_BALIK 2
#define PRIJ_BALIK 3
#define PRIO 4
#define PREDAJ 5
#define OSTATNE 6

// cas obsluhy
#define OBS_LIST 2
#define OBS_BALIK 3
#define OBS_PRIO_L 2
#define OBS_PRIO_H 30
#define OBS_OSTATNE 3
#define OBS_TLAC 0.25
#define OBS_DOCH 2
#define OBS_PREDAJ 1

// pocet liniek
#define TLACITIEK 2
double PREPAZIEK_OBYC = 2;	// univerzalnych prepaziek
double PREPAZIEK_BALIK = 2;	// univerzalna + podanie balikov
double PREPAZIEK_PRIO = 2;	// univerzalna + financne sluzby, vypisy...

// obsluzne linky
// TODO: load balancing
std::vector<Facility*> Prepazka;
Facility Tlacitka[TLACITIEK];

Histogram H_Obsluha("Histogram doby obsluhy a cakania", 0, 1, 20);
Histogram H_VyberListku("Histogram doby obsluhy stroja na vyber listkov", 0, 0.05, 12);
Histogram H_ObsDoch("Histogram doby obsluhy vyplatenia dochodku", 0, 1, 20);

Stat pod_balik("Obsluha podania balikov");
Stat prij_balik("Obsluha prijatia balikov");
Stat listy("Obsluha listovych zasielok");
Stat prio("Obsluha prioritnych sluzieb");
Stat predaj("Obsluha predaja krabic, znamok, obalok...");
Stat ostatne("Obsluha ostatnych sluzieb");
Stat dochodok("Obsluha vyplacania dochodkov");

double P_LIST = 0.44;
double P_POD_BALIK = P_LIST + 0.16;
double P_PRIJ_BALIK = P_POD_BALIK + 0.18;
double P_PRIO = P_PRIJ_BALIK + 0.013;
double P_PREDAJ = P_PRIO + 0.01;
double P_OSTATNE = 1.0 - P_LIST - P_POD_BALIK - P_PRIJ_BALIK - P_PRIO - P_PREDAJ;

// pocitadlo pre pocet pouziti niektroeho z typov prepaziek
// * sel_window[0] - obycajna prapazka
// * sel_window[1] - prepazka umoznujuca aj prioritne sluzby
// * sel_window[2] - prepazka umoznujuca aj podavanie balikov
unsigned int sel_window[3];
// pocet netrpezlivych zakaznikov
unsigned int count = 0;

class Timeout : public Event {
	Process *Id;
public:
	Timeout(double t, Process *p): Id(p) {
		Activate(Time + t);	// aktivovat v case Time+t
	}
	void Behavior() {
		count++;	// pocitadlo zakaznikov, ktori odisli
		delete Id;	// zrusenie procesu
	}
};

class Dochodok : public Process {
	double Prichod;

	void Behavior() {
		Prichod = Time;
		double obsluha = std::abs(Normal(OBS_DOCH, 1));
		Timeout *t = new Timeout(30, this);	// po 30m sa aktivuje timeout
		int selected = -1;
		unsigned int min = std::numeric_limits<unsigned int>::max();

		// vyber najkratsej fronty k tlacitkam vyvolavacieho systemu
		int id = 0;
		for (int i = 0; i < TLACITIEK; ++i) {
			if (Tlacitka[i].QueueLen() < Tlacitka[id].QueueLen())
				id = i;
		}
		Seize(Tlacitka[id]);
		Wait(Exponential(OBS_TLAC) + 0.16);
		Release(Tlacitka[id]);
		H_VyberListku(Time - Prichod);

		// zaradenie do najkratsej fronty
		int idx = 0;
		for (int i = 0; i < (PREPAZIEK_OBYC +  PREPAZIEK_PRIO + PREPAZIEK_BALIK); ++i) {
			if (Prepazka[i]->QueueLen() <= Prepazka[idx]->QueueLen() && Prepazka[i]->QueueLen() < min) {
				idx = i;
				min = Prepazka[i]->QueueLen();

				// oznacenie aky typ prepazky bol pouzity
				if (i < PREPAZIEK_OBYC)
					selected = 0;	// univerzalna prepazka
				else if (i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
					selected = 1;	// prepazka aj s prioritnymi sluzbami
				else
					selected = 2;	// prepazka aj s moznostou podania balika
			}
		}

		// cas od vyvolania cisla po dostavenie sa zakaznika
		Wait(std::abs(Normal(0.2217, 0.1652)));

		if (selected == -1) { // TODO: delete this
			std::cerr << "FATAL(in Dochodok::Behavior): nebola vybrana prepazka, neviem programovat! :'(" << std::endl;
			exit(9);
		}

		// pocitadlo pre zvoleny typ prepazky 
		sel_window[selected]++;

		Seize(*Prepazka[idx]);	// zabratie prepazky
		delete t;	// zrusenie timeoutu
		Wait(obsluha);	// prebieha obsluha
		Release(*Prepazka[idx]);	// uvolnenie prepazky

		// histogram obsluhy vyplacania dochodkov
		H_Obsluha(Time - Prichod);
		H_ObsDoch(Time - Prichod);
		// ulozenie statistik
		dochodok(obsluha);
	}
};

class ObsZakaznika : public Process {
	double Prichod;
	int Sluzba;

	void Behavior() {
		Prichod = Time;
		Timeout *t = new Timeout(30, this);	// po 30m sa aktivuje timeout
		// TODO: zobral zly listok a vrati sa po novy? 
		double obsluha;
		int selected = -1;
		unsigned int min = std::numeric_limits<unsigned int>::max();
		int idx = 0;

		// TODO: realne hodonty
		// vyber najkratsej fronty k tlacitkam vyvolavacieho systemu
		int id = 0;
		for (int i = 0; i < TLACITIEK; ++i) {
			if (Tlacitka[i].QueueLen() < Tlacitka[id].QueueLen())
				id = i;
		}
		Seize(Tlacitka[id]);
		Wait(Exponential(OBS_TLAC) + 0.16);
		Release(Tlacitka[id]);
		H_VyberListku(Time - Prichod);

		// vyber typu sluzby
		double random = Random();
		if (random <= 0.44)
			Sluzba = LIST;
		else if (random > 0.44 && random <= 0.6)
			Sluzba = POD_BALIK;
		else if (random > 0.6 && random <= 0.78)
			Sluzba = PRIJ_BALIK;
		else if (random > 0.78 && random <= 0.793) {
			// prioritne sluzby co dlho trvaju - certifikaty, vypisy atd.
			// na prepazkach 1,2 -- Sluzba = PRIO
			Sluzba = PRIO;
			Priority = 1;
		}
		else if (random > 0.793 && random <= 0.803) {
			// rychle prioritne sluzby - prodej zbozi
			// na prepazkach 4,5 (normalne prepazky) -- Sluzba = PREDAJ
			Sluzba = PREDAJ;
			Priority = 2;
		}
		else
			Sluzba = OSTATNE;

		// TODO: dat realne casy a rozlozenia
		// nastavenie doby obsluhy na zaklade zvolenej sluzby
		if (Sluzba == LIST)	{	// podanie/prijem listov
			obsluha = std::abs(Normal(OBS_LIST, 1));
			listy(obsluha);
		}
		else if (Sluzba == POD_BALIK) {	// podanie balika
			// pouzit prepazku pre podavanie balikov 'PrepazkaBalik'
			obsluha = std::abs(Normal(OBS_BALIK, 2));
			pod_balik(obsluha);
		}
		else if (Sluzba == PRIJ_BALIK) {	// prijem balika
			obsluha = std::abs(Normal(OBS_BALIK, 2));
			prij_balik(obsluha);
		}
		else if (Sluzba == PRIO) {	// prioritne sluzby (vypis z reg. trestov, financne sluzby...)
			// prioritne pouzit prepazku 'PrepazkaPrio'
			obsluha = std::abs(Normal(OBS_PRIO_H, 15));
			prio(obsluha);
		}
		else if (Sluzba == PREDAJ) {	// doplnkovy predaj - obalky, krabice...
			obsluha = std::abs(Normal(OBS_PREDAJ, 0.5));
			predaj(obsluha);
		}
		else {	// ostatne sluzby
			obsluha = std::abs(Normal(OBS_OSTATNE, 1));
			ostatne(obsluha);
		}

		// zaradenie zakaznika do najkratsej fronty podla zvolenej sluzby
		for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO + PREPAZIEK_BALIK); ++i) {
			if (Sluzba == POD_BALIK) {
				if (i == 0) {
					i = (PREPAZIEK_OBYC + PREPAZIEK_PRIO);
					idx = i;
				}
			}
			else if (Sluzba == PRIO) {
				if (i == 0) {
					i = PREPAZIEK_OBYC;
					idx = i;
				}
				if (i >= (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
					break;
			}
			if (Prepazka[i]->QueueLen() <= Prepazka[idx]->QueueLen() && Prepazka[i]->QueueLen() < min) {
				idx = i;
				min = Prepazka[i]->QueueLen();

				// oznacenie aky typ prepazky bol pouzity
				if (i < PREPAZIEK_OBYC)
					selected = 0;	// univerzalna prepazka
				else if (i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
					selected = 1;	// prepazka aj s prioritnymi sluzbami
				else
					selected = 2;	// prepazka aj s moznostou podania balika
			}
		}

		Wait(std::abs(Normal(0.2217, 0.1652))); // cas od vyvolania cisla po dostavenie sa zakaznika

		if (selected == -1) { // TODO: delete this
			std::cerr << "FATAL(in ObsZakaznika::Behavior): nebola vybrana prepazka, neviem programovat! :'(" << std::endl;
			std::cerr << "DEBUG:" << std::endl;
			std::cerr << "       Sluzba: " << Sluzba << std::endl;
			std::cerr << "       Min: " << min << std::endl;
			std::cerr << "       Idx: " << idx << std::endl;
			exit(10);
		}
		// pocitadlo pre zvoleny typ prepazky
		sel_window[selected]++;

		Seize(*Prepazka[idx]);	// zabrat prepazku
		delete t;	// zrusenie timeoutu
		Wait(obsluha);
		H_Obsluha(Time - Prichod);
		if (Sluzba == POD_BALIK)		// pri podani balika je potrebne balik umiestnit do skladu 
			Wait(Exponential(1) + 0.5);	// odnasa balik do skladu
		Release(*Prepazka[idx]);	// uvolnenie prepazky
	}
};

class Generator : public Event {
	double Interval;
	int Type;	// 0 - vseobecny zakaznici, 1 - dochodky

	void Behavior() {
		if (Type == 0)
			(new ObsZakaznika)->Activate();
		else
			(new Dochodok)->Activate();
			
		Activate(Time + Exponential(Interval));
	}
public:
	Generator(double interval, int type) : Interval(interval), Type(type) { Activate(); }
};

// TODO: urobit z toho class
// struktura pre argumenty programu
typedef struct ARGS {
	int pocet_dni;			// pocet simulovanych dni
	double prichod_zak;		// rozlozenie prichodu zakaznikov
	double prichod_doch;	// rozlozenie prichodu dochodcov
	int pocet_obyc;			// pocet prepazok pre obycajne sluzby
	int pocet_prio;			// pocet prepazok pre obycajne sluzby + prioritne sluzby
	int pocet_balik;		// pocet prepazok pre obycajne sluzby + baliky
	double p_pod_balik;		// pravdepodobnost pohladavky - podanie balikov
	double p_prij_balik;	// pravdepodobnost pohladavky - prijem balikov
	double p_list;			// pravdepodobnost pohladavky - listove sluzby
} ARGS;

int getArgs(int argc, char **argv, ARGS *arguments) {
	
	// TODO: zatial len provizorne, ak bude cas tak sa upravi alebo urobit z ARGS class
	if (argc == 10) {
		char * pEnd;
		arguments->pocet_dni = std::strtol(argv[1], &pEnd, 10);
		if (arguments->pocet_dni == -1 || *pEnd != '\0')
			arguments->pocet_dni = 1;

		arguments->prichod_zak = std::strtol(argv[2], &pEnd, 10);
		if (arguments->prichod_zak == -1 || *pEnd != '\0')
			arguments->prichod_zak = ARRIVAL;

		arguments->prichod_doch = std::strtol(argv[3], &pEnd, 10);
		if (arguments->prichod_doch == -1 || *pEnd != '\0')
			arguments->prichod_doch = PENSION;

		arguments->pocet_obyc = std::strtol(argv[4], &pEnd, 10);
		if (arguments->pocet_obyc == -1 || *pEnd != '\0')
			arguments->pocet_obyc = PREPAZIEK_OBYC;

		arguments->pocet_prio = std::strtol(argv[5], &pEnd, 10);
		if (arguments->pocet_prio == -1 || *pEnd != '\0')
			arguments->pocet_prio = PREPAZIEK_PRIO;

		arguments->pocet_balik = std::strtol(argv[6], &pEnd, 10);
		if (arguments->pocet_balik == -1 || *pEnd != '\0')
			arguments->pocet_balik = PREPAZIEK_BALIK;

		arguments->p_prij_balik = std::strtol(argv[7], &pEnd, 10);
		if (arguments->p_prij_balik == -1 || *pEnd != '\0')
			arguments->p_prij_balik = P_PRIJ_BALIK;

		arguments->p_pod_balik = std::strtol(argv[8], &pEnd, 10);
		if (arguments->p_pod_balik == -1 || *pEnd != '\0')
			arguments->p_pod_balik = P_POD_BALIK;

		arguments->p_list = std::strtol(argv[9], &pEnd, 10);
		if (arguments->p_list == -1 || *pEnd != '\0')
			arguments->p_list = P_LIST;

		return 0;
	}
	else {	// nastavit na defaultne hodnoty
		arguments->pocet_dni = 1;
		arguments->prichod_zak = ARRIVAL;
		arguments->prichod_doch = PENSION;
		arguments->pocet_obyc = PREPAZIEK_OBYC;
		arguments->pocet_prio = PREPAZIEK_PRIO;
		arguments->pocet_balik = PREPAZIEK_BALIK;
		arguments->p_prij_balik = P_PRIJ_BALIK;
		arguments->p_pod_balik = P_POD_BALIK;
		arguments->p_list = P_LIST;

		return 1;
	}
}

void setGlobals(ARGS *arguments) {
	PREPAZIEK_OBYC = arguments->pocet_obyc;
	PREPAZIEK_PRIO = arguments->pocet_prio;
	PREPAZIEK_BALIK = arguments->pocet_balik;
	P_PRIJ_BALIK = arguments->p_prij_balik;
	P_POD_BALIK = arguments->p_pod_balik;
	P_LIST = arguments->p_list;
}

int main(int argc, char **argv)
{
	// nacitanie argumentov programu
	ARGS arguments;
	if (getArgs(argc, argv, &arguments) == 0)
		setGlobals(&arguments);

	// std::cout << arguments.pocet_dni << std::endl;
	// std::cout << arguments.prichod_zak << std::endl;
	// std::cout << arguments.prichod_doch << std::endl;
	// std::cout << arguments.pocet_obyc << std::endl;
	// std::cout << arguments.pocet_prio << std::endl;
	// std::cout << arguments.pocet_balik << std::endl;

	// vytvorenie potrebneho poctu prepazok
	char const *nazov;
	for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO + PREPAZIEK_BALIK); ++i)
	{
		if (i < PREPAZIEK_OBYC)
			nazov = "Prepazka univerzalna";
		else if (i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
			nazov = "Prepazka s moznostou prioritnych sluzieb";
		else
			nazov = "Prepazka s moznostou podania balikov";

		Prepazka.push_back(new Facility(nazov));
	}
	RandomSeed(time(NULL));
	Init(0, DAY);
	// generator prichodov zakaznikov
	new Generator(arguments.prichod_zak, 0);
	// generator prichodov dochodcov pre dochodky
	if (arguments.prichod_doch != 0) {	 // simuluje sa aj vydavanie dochodkov 
		new Generator(arguments.prichod_doch, 1);
	}

	Run();
	
	// Vypis statistik a histogramov
	SetOutput("posta-prepazky.out");
	for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO + PREPAZIEK_BALIK); ++i) {
		if (i == 0)
			Print("Univerzalne prepazky\n");
		else if (i == PREPAZIEK_OBYC)
			Print("Prepazky s moznostou prioritnych sluzieb\n");
		else if (i == (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
			Print("Prepazky s moznostou podania balikov\n");
		Prepazka[i]->Output();
	}
	
	H_Obsluha.Output();
	for (int i = 0; i < 3; ++i) {
		Print("Selected%d: %d ", i, sel_window[i]);
	}
	Print("\nPocet zakaznikov, ktori odisli z fronty: %d\n", count);

	SetOutput("posta-vyvolavanie_zak.out");
	Print("Vyvolavaci system\n");
	for (int i = 0; i < TLACITIEK; ++i)
		Tlacitka[i].Output();
	H_VyberListku.Output();
	
	SetOutput("posta-dochodky.out");
	H_ObsDoch.Output();
	
	SetOutput("posta-sluzby.out");
	listy.Output();
	pod_balik.Output();
	prij_balik.Output();
	prio.Output();
	predaj.Output();
	dochodok.Output();
	ostatne.Output();

	return 0;
}
