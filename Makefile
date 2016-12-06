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
	./posta 1 -1 -1 -1 -1 -1 -1 -1 -1 0
	# vyplacanie dochodkov + load balance
	./posta 2 -1 -1 -1 -1 -1 -1 -1 -1 1
	# vianoce 1.1 - nevyplacanie dochodkov, castejsie prichody, viac listovych sluzieb a balikov
	./posta 3 0.6 0 -1 -1 -1 0.2 0.23 0.51 0
	# vianoce 1.2 - ako vianoce1 aj s vyplacanim dochodkov
	./posta 4 0.6 -1 -1 -1 -1 0.2 0.23 0.51 0
	# vianoce 2.1 - ako vianoce1 aj s vyplacanim dochodkov load balance
	./posta 5 0.6 -1 -1 -1 -1 0.2 0.23 0.51 1
	# vianoce 2.1 - +1 prepazka na baliky - 7 otvorenych
	./posta 6 0.6 0 2 2 3 0.2 0.23 0.51 0
	# vianoce 2.2 - +1 na baliky -1 czech point- 6 otvorenych
	./posta 7 0.6 0 2 1 3 0.2 0.23 0.51 0
	# vianoce 2.3 - +1 univerzalna prepazka - 7 otvorenych
	./posta 8 0.6 0 3 2 3 0.2 0.23 0.51 0
	# vianoce 2.4 - +1 na baliky +1 univerzalna a vyplacanie dochodkov - 7 otvorenych
	./posta 9 0.6 -1 3 2 3 0.2 0.23 0.51 0

clean:
	rm $(EXECUTABLE) exp*
