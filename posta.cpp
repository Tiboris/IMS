/*
 * Nazov:	posta.cpp
 * Autori:	Tibor Dudlak, xdudla00
 *			Patrik Segedy, xseged00
 * Popis:	Simulacny model posty do predmetu IMS s vyuzitim kniznice SIMLIB
 *			www.fit.vutbr.cz/~peringer/SIMLIB/
 * Datum:	6.12.2016
 */
#include "simlib.h"

#include <limits>	// pre std::numeric_limits<int>::max()
#include <iostream>	// std::cerr
#include <cmath>	// std::abs
#include <vector>
#include <string>
#include <sstream>

// 1 pracovny den v minutach - 10 hodin
#define DAY 600

// prichody zakaznikov 0.85min
#define ARRIVAL 0.85
// prichod dochodcov pre dochodky
#define PENSION 5

// sluzby
#define LIST 1
#define POD_BALIK 2
#define PRIJ_BALIK 3
#define CZP 4
#define PREDAJ 5
#define OSTATNE 6

// cas obsluhy
#define OBS_LIST 2.5
#define OBS_BALIK 2.2
#define OBS_CZP 10
#define OBS_OSTATNE 3
#define OBS_TLAC 0.25
#define OBS_DOCH 2
#define OBS_PREDAJ 1

// pocet liniek
#define ZARIADENI 2
double PREPAZIEK_OBYC = 2;	// univerzalnych prepaziek
double PREPAZIEK_BALIK = 2;	// univerzalna + podanie balikov
double PREPAZIEK_CZP = 2;	// univerzalna + financne sluzby, vypisy...

// obsluzne linky
std::vector<Facility*> Prepazka;
Facility VyvSystem[ZARIADENI];

Histogram H_Obsluha("Histogram doby stravenej v systeme", 0, 1, 30);
Histogram H_VyberListku("Histogram doby obsluhy stroja na vyber listkov", 0, 0.05, 12);
Histogram H_ObsDoch("Histogram doby obsluhy vyplatenia dochodku", 0, 1, 30);

Stat pod_balik("Obsluha podania balikov");
Stat prij_balik("Obsluha prijatia balikov");
Stat listy("Obsluha listovych zasielok");
Stat prio("Obsluha sluzieb Czech POINT");
Stat predaj("Obsluha predaja krabic, znamok, obalok...");
Stat ostatne("Obsluha ostatnych sluzieb");
Stat dochodok("Obsluha vyplacania dochodkov");

// pravdepodobnosti výberov sluzby
double P_LIST = 0.44;
double P_POD_BALIK = P_LIST + 0.16;
double P_PRIJ_BALIK = P_POD_BALIK + 0.18;
double P_CZP = P_PRIJ_BALIK + 0.013;
double P_PREDAJ = P_CZP + 0.01;
double P_OSTATNE = 1.0 - P_PREDAJ;
bool BALANCE;

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
		for (int i = 0; i < ZARIADENI; ++i) {
			if (VyvSystem[i].QueueLen() < VyvSystem[id].QueueLen())
				id = i;
		}
		Seize(VyvSystem[id]);
		Wait(Exponential(OBS_TLAC) + 0.16);
		Release(VyvSystem[id]);
		H_VyberListku(Time - Prichod);

		// zaradenie do najkratsej fronty
		int idx = 0;
		bool load_balance;

		for (int i = 0; i < (PREPAZIEK_OBYC +  PREPAZIEK_CZP + PREPAZIEK_BALIK); ++i) {
			if (BALANCE)
				load_balance = (Prepazka[i]->tstat.Number() < Prepazka[idx]->tstat.Number()	&& (Prepazka[i]->QueueLen() - Prepazka[idx]->QueueLen()) < 2);
			else
				load_balance = false;

			if ((Prepazka[i]->QueueLen() <= Prepazka[idx]->QueueLen() && Prepazka[i]->QueueLen() < min) || load_balance) {
				idx = i;
				min = Prepazka[i]->QueueLen();

				// oznacenie aky typ prepazky bol pouzity
				if (i < PREPAZIEK_OBYC)
					selected = 0;	// univerzalna prepazka
				else if (i < (PREPAZIEK_OBYC + PREPAZIEK_CZP))
					selected = 1;	// prepazka aj s prioritnymi sluzbami
				else
					selected = 2;	// prepazka aj s moznostou podania balika
			}
		}

		// cas od vyvolania cisla po dostavenie sa zakaznika
		Wait(std::abs(Normal(0.2217, 0.1652)));

		if (selected == -1) {
			std::cerr << "FATAL(in Dochodok::Behavior): nebola vybrana ziadna prepazka!" << std::endl;
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

		double obsluha;
		int selected = -1;
		unsigned int min = std::numeric_limits<unsigned int>::max();
		int idx = 0;

		// vyber najkratsej fronty k tlacitkam vyvolavacieho systemu
		int id = 0;
		for (int i = 0; i < ZARIADENI; ++i) {
			if (VyvSystem[i].QueueLen() < VyvSystem[id].QueueLen())
				id = i;
		}
		Seize(VyvSystem[id]);
		Wait(Exponential(OBS_TLAC) + 0.16);
		Release(VyvSystem[id]);
		H_VyberListku(Time - Prichod);

		// vyber typu sluzby
		double random = Random();
		if (random <= P_LIST)
			Sluzba = LIST;
		else if (random > P_LIST && random <= P_POD_BALIK)
			Sluzba = POD_BALIK;
		else if (random > P_POD_BALIK && random <= P_PRIJ_BALIK)
			Sluzba = PRIJ_BALIK;
		else if (random > P_PRIJ_BALIK && random <= P_CZP) {
			// prioritne sluzby co dlho trvaju - certifikaty, vypisy atd.
			// na prepazkach 1,2 -- Sluzba = CZP
			Sluzba = CZP;
			Priority = 1;
		}
		else if (random > P_CZP && random <= P_PREDAJ) {
			// rychle prioritne sluzby - prodej zbozi
			// na prepazkach 4,5 (normalne prepazky) -- Sluzba = PREDAJ
			Sluzba = PREDAJ;
			Priority = 2;
		}
		else
			Sluzba = OSTATNE;

		// nastavenie doby obsluhy na zaklade zvolenej sluzby
		if (Sluzba == LIST)	{	// podanie/prijem listov
			obsluha = std::abs(Normal(OBS_LIST, 1)) + 0.3;
			listy(obsluha);
		}
		else if (Sluzba == POD_BALIK) {	// podanie balika
			// pouzit prepazku pre podavanie balikov 'PrepazkaBalik'
			obsluha = std::abs(Normal(OBS_BALIK, 2)) + 0.22;
			pod_balik(obsluha);
		}
		else if (Sluzba == PRIJ_BALIK) {	// prijem balika
			obsluha = std::abs(Normal(OBS_BALIK, 2)) + 0.2;
			prij_balik(obsluha);
		}
		else if (Sluzba == CZP) {	// prioritne sluzby (vypis z reg. trestov, financne sluzby...)
			// prioritne pouzit prepazku 'PrepazkaPrio'
			obsluha = std::abs(Normal(OBS_CZP, 6)) + 6;
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
		for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_CZP + PREPAZIEK_BALIK); ++i) {
			if (Sluzba == POD_BALIK) {
				if (i == 0) {
					i = (PREPAZIEK_OBYC + PREPAZIEK_CZP);
					idx = i;
				}
			}
			else if (Sluzba == CZP) {
				if (i == 0) {
					i = PREPAZIEK_OBYC;
					idx = i;
				}
				if (i >= (PREPAZIEK_OBYC + PREPAZIEK_CZP))
					break;
			}
			bool load_balance;
			if (BALANCE)
				load_balance = (Prepazka[i]->tstat.Number() < Prepazka[idx]->tstat.Number()	&& (Prepazka[i]->QueueLen() - Prepazka[idx]->QueueLen()) < 2);
			else
				load_balance = false;

			if ((Prepazka[i]->QueueLen() <= Prepazka[idx]->QueueLen() && Prepazka[i]->QueueLen() < min)	|| load_balance) {
				idx = i;
				min = Prepazka[i]->QueueLen();

				// oznacenie aky typ prepazky bol pouzity
				if (i < PREPAZIEK_OBYC)
					selected = 0;	// univerzalna prepazka
				else if (i < (PREPAZIEK_OBYC + PREPAZIEK_CZP))
					selected = 1;	// prepazka aj s prioritnymi sluzbami
				else
					selected = 2;	// prepazka aj s moznostou podania balika
			}
		}

		Wait(std::abs(Normal(0.2217, 0.1652))); // cas od vyvolania cisla po dostavenie sa zakaznika

		if (selected == -1) {
			std::cerr << "FATAL(in ObsZakaznika::Behavior): nebola vybrana ziadna prepazka!" << std::endl;
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

// struktura pre argumenty programu
typedef struct ARGS {
	int experiment;			// pocet simulovanych dni
	double prichod_zak;		// rozlozenie prichodu zakaznikov
	double prichod_doch;	// rozlozenie prichodu dochodcov
	int pocet_obyc;			// pocet prepazok pre obycajne sluzby
	int pocet_prio;			// pocet prepazok pre obycajne sluzby + prioritne sluzby
	int pocet_balik;		// pocet prepazok pre obycajne sluzby + baliky
	double p_pod_balik;		// pravdepodobnost pohladavky - podanie balikov
	double p_prij_balik;	// pravdepodobnost pohladavky - prijem balikov
	double p_list;			// pravdepodobnost pohladavky - listove sluzby
	int balance;			// experimental load balance
} ARGS;

int getArgs(int argc, char **argv, ARGS *arguments) {

	if (argc == 11) {
		char * pEnd;
		arguments->experiment = std::strtol(argv[1], &pEnd, 10);
		if (arguments->experiment == -1 || *pEnd != '\0')
			arguments->experiment = 0;

		arguments->prichod_zak = std::strtod(argv[2], &pEnd);
		if (arguments->prichod_zak == -1 || *pEnd != '\0')
			arguments->prichod_zak = ARRIVAL;

		arguments->prichod_doch = std::strtod(argv[3], &pEnd);
		if (arguments->prichod_doch == -1 || *pEnd != '\0')
			arguments->prichod_doch = PENSION;

		arguments->pocet_obyc = std::strtol(argv[4], &pEnd, 10);
		if (arguments->pocet_obyc == -1 || *pEnd != '\0')
			arguments->pocet_obyc = PREPAZIEK_OBYC;

		arguments->pocet_prio = std::strtol(argv[5], &pEnd, 10);
		if (arguments->pocet_prio == -1 || *pEnd != '\0')
			arguments->pocet_prio = PREPAZIEK_CZP;

		arguments->pocet_balik = std::strtol(argv[6], &pEnd, 10);
		if (arguments->pocet_balik == -1 || *pEnd != '\0')
			arguments->pocet_balik = PREPAZIEK_BALIK;

		arguments->p_prij_balik = std::strtod(argv[7], &pEnd);
		if (arguments->p_prij_balik == -1 || *pEnd != '\0')
			arguments->p_prij_balik = 0.18;

		arguments->p_pod_balik = std::strtod(argv[8], &pEnd);
		if (arguments->p_pod_balik == -1 || *pEnd != '\0')
			arguments->p_pod_balik = 0.16;

		arguments->p_list = std::strtod(argv[9], &pEnd);
		if (arguments->p_list == -1 || *pEnd != '\0')
			arguments->p_list = P_LIST;

		arguments->balance = std::strtol(argv[10], &pEnd, 10);
		if (arguments->balance == -1 || *pEnd != '\0')
			arguments->balance = 0;

		return 0;
	}
	else {	// nastavit na defaultne hodnoty
		arguments->experiment = 0;
		arguments->prichod_zak = ARRIVAL;
		arguments->prichod_doch = 0;
		arguments->pocet_obyc = PREPAZIEK_OBYC;
		arguments->pocet_prio = PREPAZIEK_CZP;
		arguments->pocet_balik = PREPAZIEK_BALIK;
		arguments->p_prij_balik = P_PRIJ_BALIK;
		arguments->p_pod_balik = P_POD_BALIK;
		arguments->p_list = P_LIST;
		arguments->balance = 0;

		return 1;
	}
}

void setGlobals(ARGS *arguments) {
	PREPAZIEK_OBYC = arguments->pocet_obyc;
	PREPAZIEK_CZP = arguments->pocet_prio;
	PREPAZIEK_BALIK = arguments->pocet_balik;
	P_LIST = arguments->p_list;
	P_POD_BALIK = P_LIST + arguments->p_pod_balik;
	P_PRIJ_BALIK = P_POD_BALIK + arguments->p_prij_balik;
	P_CZP = P_PRIJ_BALIK + 0.013;
	P_PREDAJ = P_CZP + 0.01;
	P_OSTATNE = 1.0 - P_PREDAJ;
	BALANCE = arguments->balance;
	if (P_OSTATNE < 0) {
		std::cerr << "Zle nastavene pravdepodobnosti sluzieb!" << std::endl;
		exit(2);
	}
}

int main(int argc, char **argv)
{
	// nacitanie argumentov programu
	ARGS arguments;
	if (getArgs(argc, argv, &arguments) == 0)
		setGlobals(&arguments);

	// vytvorenie potrebneho poctu prepazok
	char const *nazov;
	for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_CZP + PREPAZIEK_BALIK); ++i)
	{
		if (i < PREPAZIEK_OBYC)
			nazov = "Prepazka univerzalna";
		else if (i < (PREPAZIEK_OBYC + PREPAZIEK_CZP))
			nazov = "Prepazka s moznostou sluzieb Czech POINT";
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

	// nastavenie nazvu vystupneho suboru
	std::ostringstream ss;
	ss << "exp" << arguments.experiment << "-posta.out";
	std::string filename = ss.str();
	SetOutput(filename.c_str());

	// Vypis statistik a histogramov
	for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_CZP + PREPAZIEK_BALIK); ++i) {
		if (i == 0)
			Print("Univerzalne prepazky\n");
		else if (i == PREPAZIEK_OBYC)
			Print("Prepazky s moznostou sluzieb Czech POINT\n");
		else if (i == (PREPAZIEK_OBYC + PREPAZIEK_CZP))
			Print("Prepazky s moznostou podania balikov\n");
		Prepazka[i]->Output();
	}

	H_Obsluha.Output();

	Print("Vyvolavaci system\n");
	for (int i = 0; i < ZARIADENI; ++i)
		VyvSystem[i].Output();
	H_VyberListku.Output();

	H_ObsDoch.Output();

	listy.Output();
	pod_balik.Output();
	prij_balik.Output();
	prio.Output();
	predaj.Output();
	dochodok.Output();
	ostatne.Output();

	Print("Pocet poziadavok spracovanych typom prepazky\n");
	for (int i = 0; i < 3; ++i) {
		Print("Typ %d: %d ", i, sel_window[i]);
	}
	Print("\nPocet zakaznikov, ktori odisli z fronty: %d\n", count);

	return 0;
}
