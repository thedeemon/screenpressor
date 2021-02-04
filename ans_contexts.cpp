#include "ans_contexts.h"

void Context::updateC1(BYTE c) {
	switch(u.c1.lst.findOrAdd(c, u.c1.d)) {
	case Found:
		if (u.c1.d <= 4) u.c4 = upgrade<Cx1, Cx4>(u.c1, c); 
		else u.c5 = upgrade<Cx1, Cx5>(u.c1, c);
		break;
	case Added: u.c1.d++; break;
	case NoRoom: u.c2 = upgrade<Cx1, Cx2>(u.c1, c); break;
	}
}

void Context::updateC2(BYTE c, bool decoding) {
	switch(u.c2.symb->findOrAdd(c, u.c2.d)) {
	case Found:	
		u.c6 = upgrade<Cx2, Cx6>(u.c2, c);	
		if (decoding) u.c6.sortByFreqs();
		break;
	case Added: u.c2.d++; break;
	case NoRoom: u.c3 = upgrade<Cx2, Cx3>(u.c2, c); break;
	}
}

void Context::updateC3(BYTE c, bool decoding) {
	switch(u.c3.symb->findOrAdd(c, u.c3.d)) {
	case Found:	u.c7 = upgradeTo7<Cx3>(u.c3, c, decoding); break;
	case Added: u.c3.d++; break;
	case NoRoom: assert(0 && "c3.findOrAdd returned NoRoom"); break;
	}
}


bool Context::encode(BYTE c, Freq &interval) {
	switch(u.c1.kind) {
	case 0: u.c1.create(c); return false;
	case 1: updateC1(c);	return false; // kind can change to 2, 4 or 5
	case 2: updateC2(c, false); return false; // kind can change to 3 or 6
	case 3: updateC3(c, false); return false; // kind can change to 7
	case 4: if (!u.c4.encode(c, interval)) u.c5 = upgrade<Cx4, Cx5>(u.c4, c); 				
			return true;
	case 5:	if (!u.c5.encode(c, interval)) u.c6 = upgrade<Cx5, Cx6>(u.c5, c);
			return true;
	case 6:	if (!u.c6.encode(c, interval)) u.c7 = upgradeTo7<Cx6>(u.c6, c, false);
			return true;
	case 7:	u.c7.encode(c, interval); return true;
	default: assert(0 && "bad kind value in Context.encode");
	}//switch kind
	return false;
}

void Context::update(BYTE c) { //decode did not know the symbol
	switch(u.c1.kind) {
	case 0: u.c1.create(c); break;
	case 1: updateC1(c); break;	// kind can change to 2, 4 or 5		
	case 2: updateC2(c, true); break; // -> 3 or 6
	case 3: updateC3(c, true); break; // -> 7
	}
}

bool Context::decode(int someFreq, BYTE &c, Freq & interval) {
	if (u.c1.kind < 4) return false;
	switch(u.c1.kind) {
	case 4: if (!u.c4.decode(someFreq, c, interval)) u.c5 = upgrade<Cx4, Cx5>(u.c4, c); 
		break;
	case 5: if (!u.c5.decode(someFreq, c, interval)) { u.c6 = upgrade<Cx5, Cx6>(u.c5, c); u.c6.sortByFreqs(); }
		break;
	case 6:	if (!u.c6.decode(someFreq, c, interval)) u.c7 = upgradeTo7<Cx6>(u.c6, c, true);
		break;
	case 7: u.c7.decode(someFreq, c, interval); break;
	default: assert(0=="unexpected kind in Context.decode");
	}
	return true;
}

void Context::free() {
	switch(u.c1.kind) {
	case 2: u.c2.free(); break;
	case 3: u.c3.free(); break;
	case 5: u.c5.free(); break;
	case 6: u.c6.free(); break;
	case 7: u.c7.free(); break;
	}
}