#include "simlib.h"

#include <limits>	// pre std::numeric_limits<int>::max()
#include <iostream>	// std::cerr
#include <cmath>	// std::abs

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

// kapacity
#define PREPAZIEK_OBYC 2	// univerzalnych prepaziek
#define PREPAZIEK_BALIK 2	// univerzalna + podanie balikov
#define PREPAZIEK_PRIO 2	// univerzalna + financne sluzby, vypisy...
#define TLACITIEK 2

// globalne objekty
// TODO: load balancing
Facility Prepazka[PREPAZIEK_OBYC + PREPAZIEK_PRIO + PREPAZIEK_BALIK];
Facility Tlacitka[TLACITIEK];

Histogram H_Obsluha("Histogram doby obsluhy a cakania", 0, 1, 20);
Histogram H_VyberListku("Histogram doby obsluhy stroja na vyber listkov", 0, 0.05, 12);
Histogram H_ObsDoch("Histogram doby obsluhy vyplatenia dochodku", 0, 1, 20);

Stat pod_balik("Obsluha podania balikov");
Stat prij_balik("Obsluha prijatia balikov");
Stat listy("Obsluha listovych zasielok");
Stat prio("Obsluha prioritnych sluzieb");
Stat predaj("Obsluha predaja kolkov, znamok, obalok...");
Stat ostatne("Obsluha ostatnych sluzieb");
Stat dochodok("Obsluha vyplacania dochodkov");

// using namespace std;

unsigned int sel_window[3];
unsigned int count = 0;

class Timeout : public Event {
	Process *Id;
public:
	Timeout(double t, Process *p): Id(p) {
		Activate(Time + t);	// aktivovat v case Time+t
	}
	void Behavior() {
		count++;	// pocitadlo zakaznikov, ktori odisli
		// Id->Out();	// vyhodit z fronty
		delete Id;	// zrusenie procesu
		// Cancel();	// zrusenie udalosti
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

		int id = 0;
		for (int i = 0; i < TLACITIEK; ++i) {
			if (Tlacitka[i].QueueLen() < Tlacitka[id].QueueLen())
				id = i;
		}
		Seize(Tlacitka[id]);
		Wait(Exponential(OBS_TLAC) + 0.16);
		Release(Tlacitka[id]);
		H_VyberListku(Time - Prichod);

		int idx = 0;
		for (int i = 0; i < (PREPAZIEK_OBYC +  PREPAZIEK_PRIO + PREPAZIEK_BALIK); ++i) {
			if (Prepazka[i].QueueLen() <= Prepazka[idx].QueueLen() && Prepazka[i].QueueLen() < min) {
				idx = i;
				min = Prepazka[i].QueueLen();

				if (i < PREPAZIEK_OBYC)
					selected = 0;
				else if (i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
					selected = 1;
				else
					selected = 2;
			}
		}

		// cas od vyvolania cisla po dostavenie sa zakaznika
		Wait(std::abs(Normal(0.17, 0.08)));

		if (selected == -1) { // TODO: delete this
			std::cerr << "FATAL(in Dochodok::Behavior): nebola vybrana prepazka, neviem programovat! :'(" << std::endl;
			exit(9);
		}

		sel_window[selected]++;

		Seize(Prepazka[idx]);
		delete t;	// zrusenie timeoutu
		Wait(obsluha);
		Release(Prepazka[idx]);

		// histogram obsluhy vyplacania dochodkov
		H_Obsluha(Time - Prichod);
		H_ObsDoch(Time - Prichod);
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

		// TODO: realne hodonty
		// vyber listku
		int id = 0;
		for (int i = 0; i < TLACITIEK; ++i) {
			if (Tlacitka[i].QueueLen() < Tlacitka[id].QueueLen())
				id = i;
		}
		Seize(Tlacitka[id]);
		Wait(Exponential(OBS_TLAC) + 0.16);
		Release(Tlacitka[id]);
		H_VyberListku(Time - Prichod);

		// Sluzba = Uniform(1,6);
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
		if (Sluzba == LIST)	{	// podanie/prijem listov
			obsluha = std::abs(Normal(OBS_LIST, 1));
			listy(obsluha);
		}
		else if (Sluzba == POD_BALIK) {	// podanie balika
			// pouzit prepazku pre podavanie balikov 'PrepazkaBalik'
			obsluha = std::abs(Normal(OBS_BALIK, 2));
			pod_balik(obsluha);
		}
		else if (Sluzba == PRIJ_BALIK) {// prijem balika
			obsluha = std::abs(Normal(OBS_BALIK, 2));
			prij_balik(obsluha);
		}
		else if (Sluzba == PRIO) {		// prioritne sluzby (vypis z reg. trestov, financne sluzby...)
			// prioritne pouzit prepazku 'PrepazkaPrio'
			obsluha = std::abs(Normal(OBS_PRIO_H, 15));
			prio(obsluha);
		}
		else if (Sluzba == PREDAJ) {
			obsluha = std::abs(Normal(OBS_PREDAJ, 0.5));
			predaj(obsluha);
		}
		else {
			obsluha = std::abs(Normal(OBS_OSTATNE, 1));
			ostatne(obsluha);
		}

		int selected = -1;
		unsigned int min = std::numeric_limits<unsigned int>::max();
		int idx = 0;

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
			if (Prepazka[i].QueueLen() <= Prepazka[idx].QueueLen() && Prepazka[i].QueueLen() < min) {
				idx = i;
				min = Prepazka[i].QueueLen();

				if (i < PREPAZIEK_OBYC)
					selected = 0;
				else if (i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
					selected = 1;
				else
					selected = 2;
			}
		}

		// TODO: wait -> cesta ku okienku
		// 0.2217, 0.1652
		Wait(std::abs(Normal(0.17, 0.08))); // cas od vyvolania cisla po dostavenie sa zakaznika

		if (selected == -1) { // TODO: delete this
			std::cerr << "FATAL(in ObsZakaznika::Behavior): nebola vybrana prepazka, neviem programovat! :'(" << std::endl;
			std::cerr << "DEBUG:" << std::endl;
			std::cerr << "       Sluzba: " << Sluzba << std::endl;
			std::cerr << "       Min: " << min << std::endl;
			std::cerr << "       Idx: " << idx << std::endl;
			exit(10);
		}

		sel_window[selected]++;

		Seize(Prepazka[idx]);
		delete t;	// zrusenie timeoutu
		Wait(obsluha);
		Release(Prepazka[idx]);

		H_Obsluha(Time - Prichod);
	}
};

class Generator : public Event {
	double Interval;
	int Type;

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

int main(int argc, char **argv)
{
	RandomSeed(time(NULL));
	Init(0, DAY);
	// generator prichodov zakaznikov
	new Generator(ARRIVAL, 0);
	// generator prichodov dochodcov pre dochodky
	new Generator(PENSION, 1);
	Run();
	
	for (int i = 0; i < (PREPAZIEK_OBYC + PREPAZIEK_PRIO + PREPAZIEK_BALIK); ++i) {
		if (i == 0)
			Print("Univerzalne prepazky\n");
		else if (i == PREPAZIEK_OBYC)
			Print("Prepazky s moznostou prioritnych sluzieb\n");
		else if (i == (PREPAZIEK_OBYC + PREPAZIEK_PRIO))
			Print("Prepazky s moznostou podania balikov\n");
		Prepazka[i].Output();
	}
	
	H_Obsluha.Output();
	Print("Stroj na listky\n");
	for (int i = 0; i < TLACITIEK; ++i)
		Tlacitka[i].Output();
	H_VyberListku.Output();
	H_ObsDoch.Output();
	
	for (int i = 0; i < 3; ++i) {
		Print("Selected%d: %d ", i, sel_window[i]);
	}
	Print("\nPocet zakaznikov, ktori odisli z fronty: \n", count);

	listy.Output();
	pod_balik.Output();
	prij_balik.Output();
	prio.Output();
	predaj.Output();
	dochodok.Output();
	ostatne.Output();

	SIMLIB_statistics.Output();

	return 0;
}
