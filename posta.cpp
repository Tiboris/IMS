#include "simlib.h"

#include <limits>	// pre std::numeric_limits<int>::max()
#include <iostream>
#include <cmath>

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
#define OSTATNE 5

// cas obsluhy
#define OBS_LIST 2
#define OBS_BALIK 3
#define OBS_PRIO_L 2
#define OBS_PRIO_H 30
#define OBS_OSTATNE 3
#define OBS_TLAC 0.25
#define OBS_DOCH 2

// kapacity
#define PREPAZIEK_OBYC 2	// univerzalnych prepaziek
#define PREPAZIEK_BALIK 2	// univerzalna + podanie balikov
#define PREPAZIEK_PRIO 2	// univerzalna + financne sluzby, vypisy...
#define TLACITIEK 2

// globalne objekty
// TODO: do dvojrozmerneho pola
// TODO: load balancing


Facility Prepazka[PREPAZIEK_OBYC + PREPAZIEK_PRIO + PREPAZIEK_BALIK];
// Facility PrepazkaObyc[PREPAZIEK_OBYC];
// Facility PrepazkaBalik[PREPAZIEK_BALIK];
// Facility PrepazkaPrio[PREPAZIEK_PRIO];

Facility Tlacitka[TLACITIEK];

Histogram H_Obsluha("Histogram doby obsluhy a cakania", 0, 1, 20);
Histogram H_VyberListku("Histogram doby obsluhy stroja na vyber listkov", 0, 0.05, 12);
Histogram H_ObsDoch("Histogram doby obsluhy vyplatenia dochodku", 0, 1, 20);

using namespace std;

//DEBUG
uint listy = 0, pod_balik = 0, prij_balik = 0, prio = 0, ostatne = 0;
uint sel_window[3];
uint s0 = 0, s1 = 0, s2 = 0;
uint count = 0, dochodok = 0;

class Timeout : public Event {
	Process *Id;
public:
	Timeout(double t, Process *p): Id(p) {
		Activate(Time + t);	// aktivovat v case Time+t
	}
	void Behavior() {
		count++;	// pocitadlo zakaznikov, ktori odisli
		Id->Out();	// vyhodit z fronty
		Id->Cancel();	// zrusenie procesu
		Cancel();	// zrusenie udalosti
	}
};

class Dochodok : public Process {
	double Prichod;

	void Behavior() {
		Prichod = Time;
		dochodok++;
		Timeout *t = new Timeout(30, this);	// po 30m sa aktivuje timeout
		unsigned int selected = -1, min = std::numeric_limits<unsigned int>::max();

		// TODO: samostatny proces???
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
		Wait(abs(Normal(0.17, 0.08)));

		if (selected == -1) { // TODO: delete this
			cerr << "FATAL(in Dochodok::Behavior): nebola vybrana prepazka, neviem programovat! :'(" << endl;
			exit(9);
		}

		sel_window[selected]++;

		Seize(Prepazka[idx]);
		t->Cancel();	// zrusenie timeoutu
		Wait(abs(Normal(OBS_DOCH, 1)));
		Release(Prepazka[idx]);

		// histogram obsluhy vyplacania dochodkov
		H_ObsDoch(Time - Prichod);
	}
};

class ObsZakaznika : public Process {
	double Prichod;
	int Sluzba;

	void Behavior() {
		Prichod = Time;
		Timeout *t = new Timeout(30, this);	// po 30m sa aktivuje timeout
		// zobral zly listok?
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

		// TODO: prioritne sluzby co dlho trvaju - certifikaty, vypisy atd.
		//		 na prepazkach 1,2 -- Sluzba = PRIO
		// TODO: rychle prioritne sluzby - prodej zbozi
		//		 na prepazkach 4,5 (normalne prepazky) -- Sluzba = LIST???

		// Sluzba = Uniform(1,6);
		double random = Random();
		if (random <= 0.44)
			Sluzba = LIST;
		else if (random > 0.44 && random <= 0.6)
			Sluzba = POD_BALIK;
		else if (random > 0.6 && random <= 0.78)
			Sluzba = PRIJ_BALIK;
		else if (random > 0.78 && random <= 0.793)
			Sluzba = PRIO;
		else
			Sluzba = OSTATNE;

		// TODO: dat realne casy a rozlozenia
		if (Sluzba == LIST)	{	// podanie/prijem listov
			obsluha = abs(Normal(OBS_LIST, 1));
			listy++;
		}
		else if (Sluzba == POD_BALIK) {	// podanie balika
			// pouzit prepazku pre podavanie balikov 'PrepazkaBalik'
			obsluha = abs(Normal(OBS_BALIK, 2));
			pod_balik++;
		}
		else if (Sluzba == PRIJ_BALIK) {// prijem balika
			obsluha = abs(Normal(OBS_BALIK, 2));
			prij_balik++;
		}
		else if (Sluzba == PRIO) {		// prioritne sluzby (vypis z reg. trestov, financne sluzby...)
			// prioritne pouzit prepazku 'PrepazkaPrio'
			obsluha = abs(Normal(OBS_PRIO_H, 15));
			prio++;
		}
		else {
			obsluha = abs(Normal(OBS_OSTATNE, 1));
			ostatne++;
		}

		unsigned int selected = -1, min = std::numeric_limits<unsigned int>::max();
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
		Wait(abs(Normal(0.17, 0.08))); // cas od vyvolania cisla po dostavenie sa zakaznika

		if (selected == -1) { // TODO: delete this
			cerr << "FATAL(in ObsZakaznika::Behavior): nebola vybrana prepazka, neviem programovat! :'(" << endl;
			cerr << "DEBUG:" << endl;
			cerr << "       Sluzba: " << Sluzba << endl;
			cerr << "       Min: " << Min << endl;
			cerr << "       Idx: " << idx << endl;
			exit(10);
		}

		sel_window[selected]++;

		Seize(Prepazka[idx]);
		t->Cancel();	// zrusenie timeoutu
		Wait(obsluha);
		Release(Prepazka[idx]);

		H_Obsluha(Time - Prichod);
	}
};

class Generator : public Event {
	void Behavior() {
		(new ObsZakaznika)->Activate();
		Activate(Time + Exponential(ARRIVAL));
	}
};

class GenDochodkov : public Event {
	void Behavior() {
		(new Dochodok)->Activate();
		Activate(Time + Exponential(PENSION));
	}
};

int main(int argc, char const *argv[])
{
	RandomSeed(time(NULL));
	Init(0, 600);
	(new Generator)->Activate();
	(new GenDochodkov)->Activate();
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
	
	cout << "list: " << listy << " pod_balik: " << pod_balik << " prij_balik: " << prij_balik << " prio: " << prio << " ostatne: " << ostatne << endl;
	for (int i = 0; i < 3; ++i) {
		cout << " sel" << i << ": " << sel_window[i];
	}
	cout << "\nPocet zakaznikov, ktori odisli z fronty: " << count << endl;
	cout << "Dochodkov: " << dochodok << endl;

	return 0;
}
