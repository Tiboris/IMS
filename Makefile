CXX=g++
CXXFLAGS=-g -pedantic -Wall -Wextra -O2 -std=c++11
LDFLAGS=-lsimlib -lm
SOURCES=posta.cpp
EXECUTABLE=posta

all:$(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

run:
	# Argumenty programu:
	# 	1	- cislo experimentu
	# 	2	- prichody zakaznikov
	# 	3 	- prichody dochodcov -- hodnota 0 -> dochodky sa nebudu vyplacat
	#	4	- pocet prepazok pre obycajne sluzby
	#	5	- pocet prepazok pre obycajne sluzby + prioritne sluzby
	#	6	- pocet prepazok pre obycajne sluzby + baliky
	#	7	- pravdepodobnost pohladavky - podanie balikov
	#	8	- pravdepodobnost pohladavky - prijem balikov
	#	9	- pravdepodobnost pohladavky - listove sluzby
	#
	# Hodnota -1 -> defaultna hodnota
	#
	# zakladny model
	./posta
	# vyplacanie dochodkov
	./posta 1 -1 -1 -1 -1 -1 -1 -1 -1
	# vianoce 1 - nevyplacanie dochodkov, castejsie prichody, viac listovych sluzieb a balikov
	./posta 2 0.6 0 -1 -1 -1 0.2 0.23 0.51
	# vianoce 2 - ako vianoce1 aj s vyplacanim dochodkov - otvorene 10/10
	./posta 3 0.6 0 6 2 2 0.2 0.23 0.51
	# vianoce 1.1 - +1 univerzalna prepazka - 7/10 otvorenych
	./posta 4 0.6 0 3 -1 -1 0.2 0.23 0.51
	# vianoce 1.2 - +2 univerzalna prepazka - 8/10 otvorenych
	./posta 5 0.6 0 4 -1 -1 0.2 0.23 0.51
	# vianoce 1.3 - +3 univerzalna prepazka - 9/10 otvorenych
	./posta 6 0.6 0 5 -1 -1 0.2 0.23 0.51
	
	#
	# TODO: DALSIE ???
	# ./posta 7 -1 -1 -1 -1 -1 -1 -1 -1
	# ./posta 8 -1 -1 -1 -1 -1 -1 -1 -1
	# ./posta 9 -1 -1 -1 -1 -1 -1 -1 -1

clean:
	rm $(EXECUTABLE) exp*