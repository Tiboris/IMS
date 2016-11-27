#include "simlib.h"

#include <limits>	// pre std::numeric_limits<int>::max()
#include <iostream>

// prichody zakaznikov 0.78min = 46.8s
#define ARRIVAL 0.85

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

// kapacity
#define PREPAZIEK_OBYC 5	// univerzalnych prepaziek
#define PREPAZIEK_BALIK 2	// univerzalna + podanie balikov
#define PREPAZIEK_PRIO 2	// univerzalna + financne sluzby, vypisy...
#define TLACITIEK 2

// globalne objekty
Facility PrepazkaObyc[PREPAZIEK_OBYC];
Facility PrepazkaBalik[PREPAZIEK_BALIK];
Facility PrepazkaPrio[PREPAZIEK_PRIO];

Facility Tlacitka[TLACITIEK];

Histogram H_Obsluha("Histogram doby obsluhy a cakania", 0, 1, 20);
Histogram H_VyberListku("Histogram doby obsluhy stroja na vyber listkov", 0, 0.05, 12);

using namespace std;

//DEBUG
uint listy = 0, pod_balik = 0, prij_balik = 0, prio = 0, ostatne = 0;
uint s0 = 0, s1 = 0, s2 = 0;
uint count = 0;

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

class ObsZakaznika : public Process {
	double Prichod;
	int Sluzba;

	void Behavior() {
		Prichod = Time;
		Timeout *t = new Timeout(30, this);	// po 30m sa aktivuje timeout
		// timeout?
		// zobral zly listok?
		double obsluha;

		// vyber sluzby
		// TODO: dat wait, realne hodonty
		// vyber listku
		int id = 0;
		for (int i = 0; i < TLACITIEK; ++i) {
			if (Tlacitka[i].QueueLen() < Tlacitka[id].QueueLen())
				id = i;
		}
		Seize(Tlacitka[id]);
		Wait(Exponential(OBS_TLAC));
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
		else if (random > 0.78 && random <= 0.793)
			Sluzba = PRIO;
		else
			Sluzba = OSTATNE;

		// TODO: dat realne casy a rozlozenia
		if (Sluzba == LIST)	{// podanie/prijem listov
			obsluha = Exponential(OBS_LIST);
			listy++;
		}
		else if (Sluzba == POD_BALIK) {	// podanie balika
			// pouzit prepazku pre podavanie balikov 'PrepazkaBalik'
			obsluha = Exponential(OBS_BALIK);
			pod_balik++;
		}
		else if (Sluzba == PRIJ_BALIK) {// prijem balika
			obsluha = Exponential(OBS_BALIK);
			prij_balik++;
		}
		else if (Sluzba == PRIO) {		// prioritne sluzby (vypis z reg. trestov, financne sluzby...)
			// prioritne pouzit prepazku 'PrepazkaPrio'
			obsluha = Exponential(OBS_PRIO_H);
			prio++;
		}
		else {
			obsluha = Exponential(OBS_OSTATNE);
			ostatne++;
		}

		unsigned int selected = -1, min = std::numeric_limits<unsigned int>::max();
		int idx[3] = {0};

		if (Sluzba != PRIO) {
			for (int i = 0; i < PREPAZIEK_BALIK; ++i) {
				if (PrepazkaBalik[i].QueueLen() < PrepazkaBalik[idx[2]].QueueLen() && PrepazkaBalik[i].QueueLen() <= min) {
					idx[2] = i;
					min = PrepazkaObyc[i].QueueLen();
					selected = 2;
				}
			}
		}
		if (Sluzba != PRIO && Sluzba != POD_BALIK) {
			for (int i = 0; i < PREPAZIEK_OBYC; ++i) {
				if (PrepazkaObyc[i].QueueLen() < PrepazkaObyc[idx[0]].QueueLen() && PrepazkaObyc[i].QueueLen() <= min) {
					idx[0] = i;
					min = PrepazkaObyc[i].QueueLen();
					selected = 0;
				}
			}
		}
		if (Sluzba != POD_BALIK) {
			for (int i = 0; i < PREPAZIEK_PRIO; ++i) {
				if (PrepazkaPrio[i].QueueLen() < PrepazkaPrio[idx[1]].QueueLen() && PrepazkaPrio[i].QueueLen() <= min) {
					idx[1] = i;
					min = PrepazkaObyc[i].QueueLen();
					selected = 1;
				}
			}
		}

		if (selected == -1) {
			selected = 0;
			if (Sluzba == PRIO)
				selected = 1;
			if (Sluzba == POD_BALIK)
				selected = 2;
		}

		if (selected == 0) {
			s0++;
			Seize(PrepazkaObyc[idx[selected]]);
			t->Cancel();	// zrusenie timeoutu
			Wait(obsluha);
			Release(PrepazkaObyc[idx[selected]]);
		}
		else if (selected == 1) {
			s1++;
			Seize(PrepazkaPrio[idx[selected]]);
			t->Cancel();	// zrusenie timeoutu
			Wait(obsluha);
			Release(PrepazkaPrio[idx[selected]]);
		}
		else if (selected == 2) {
			s2++;
			Seize(PrepazkaBalik[idx[selected]]);
			t->Cancel();	// zrusenie timeoutu
			Wait(obsluha);
			Release(PrepazkaBalik[idx[selected]]);
		}
		H_Obsluha(Time - Prichod);

		// cout << "Sluzba: " << Sluzba << endl;
		// cout << "selected: " << selected << endl;
		// cout << "obsluha: " << obsluha << endl;
		if (selected == -1)
		{
			cout << "ZLEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE" << endl;
		}
	}
};

class Generator : public Event {
	void Behavior() {
		(new ObsZakaznika)->Activate();
		Activate(Time + Exponential(ARRIVAL));
	}
};

int main(int argc, char const *argv[])
{
	Init(0, 600);
	(new Generator)->Activate();
	Run();
	
	Print("Univerzalne prepazky\n");
	for (int i = 0; i < PREPAZIEK_OBYC; ++i)
		PrepazkaObyc[i].Output();
	Print("Prepazky s moznostou podania balikov\n");
	for (int i = 0; i < PREPAZIEK_BALIK; ++i)
		PrepazkaBalik[i].Output();
	Print("Prepazky s moznostou prioritnych sluzieb\n");
	for (int i = 0; i < PREPAZIEK_PRIO; ++i)
		PrepazkaPrio[i].Output();
	H_Obsluha.Output();
	Print("Stroj na listky\n");
	for (int i = 0; i < TLACITIEK; ++i)
		Tlacitka[i].Output();
	H_VyberListku.Output();
	cout << "list: " << listy << " pod_balik: " << pod_balik << " prij_balik: " << prij_balik << " prio: " << prio << " ostatne: " << ostatne << endl;
	cout << "0: " << s0 << " 1: " << s1 << " 2: " << s2 << endl;
	cout << "Pocet zakaznikov, ktori odisli z fronty: " << count << endl;
	return 0;
}