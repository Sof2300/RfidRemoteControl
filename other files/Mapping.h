#ifndef MAPPING_H
#define MAPPING_H



class Expression {
protected:
	Expression *parent=0;
	DataMap *datamap;
public:
	Expression(){}
	void setParent(Expression *parent1){parent=parent1;}


	virtual void setDataMap(DataMap *datamap1) {datamap=datamap1;}
	virtual bool eval(){return false;};
	virtual bool update(){
		if(parent) return parent->update();
		else {eval();return true;}
	};
	virtual float evalFloat(){return (float)eval();}
	virtual String evalString(){return String(evalFloat());}



	bool isLink(String str) {
		if(str.length()>0 && str[0]=='/') return true;
		return false;
	}
};



///////////////////////////////////////
class Ifthenelse : public Expression {
	Expression *conditionexp,*thenexp,*elseexp;
public:

	Ifthenelse(Expression *conditionexp0, Expression *thenexp0,Expression *elseexp0=0):
		conditionexp(conditionexp0), thenexp(thenexp0), elseexp(elseexp0) {
		if(conditionexp) conditionexp->setParent(this);
		if(thenexp) thenexp->setParent(this);
		if(elseexp) elseexp->setParent(this);
	}

	virtual void setDataMap(DataMap *datamap1) {
		datamap=datamap1;
		if(conditionexp) conditionexp->setDataMap(datamap);
		if(thenexp) thenexp->setDataMap(datamap);
		if(elseexp) elseexp->setDataMap(datamap);
	}

	bool eval(){
		if(!conditionexp) return false;
		bool b=conditionexp->eval();
		if(b) {if(thenexp) thenexp->eval();}
		else {if(elseexp) elseexp->eval();}
		return b;
	}
} ;



///////////////////////////////////////
class Comparison : public Expression {
	String comparator;
	Expression *leftside,*rightside;
public:
	Comparison(String comparator0,Expression * leftside0, Expression *rightside0):
		comparator(comparator0), leftside(leftside0), rightside(rightside0){
		if(leftside) leftside->setParent(this);
		if(rightside) rightside->setParent(this);
	}
	virtual void setDataMap(DataMap *datamap1) {
		datamap=datamap1;
		if(leftside) leftside->setDataMap(datamap);
		if(rightside) rightside->setDataMap(datamap);
	}

	bool eval() {
		if(!leftside || !rightside) return false;
		if(comparator=="==") return leftside->evalFloat()==rightside->evalFloat();
		if(comparator==">") return leftside->evalFloat()>rightside->evalFloat();
		if(comparator==">=") return leftside->evalFloat()>=rightside->evalFloat();
		if(comparator=="<") return leftside->evalFloat()<rightside->evalFloat();
		if(comparator=="<=") return leftside->evalFloat()<=rightside->evalFloat();
		return false;
	}
};



///////////////////////////////////////
class Transfer : public Expression/*, public EventListener*/ {
	String path;
	Expression *srcexp;
public:
	Transfer(String pathdest, Expression *source): path(pathdest), srcexp(source){
		if(srcexp) srcexp->setParent(this);
	};

	bool eval(){
		if(!datamap || path.length()==0) return false;
		String dstval=srcexp->evalString();
		if(datamap->get(path)!=dstval) datamap->update(path,dstval);
		return false;
	}

};








#define MAXLINKRECURS 255
///////////////////////////////////////
class VarExpression : public Expression, public EventListener {
	String path;
	unsigned int recurs=0;
	std::vector<String> subpaths;

public:

	VarExpression(String pathsrc):path(pathsrc) {};
	bool notify(GenString ename, Event *e){return notify(String(ename.c_str()),e);}
	bool notify(String ename, Event *e){
		if(ename!="/data/humidity/value"){
			Serial.println();
		}
		updateLinkSubscriptions();
		return update();
	}
	bool update(){if(parent) return parent->update(); else return false;};
	virtual void setDataMap(DataMap *datamap1) {
		datamap=datamap1;
		if(datamap) {datamap->on(path.c_str(),this);updateLinkSubscriptions();}
	}

	void setPath(String npath){
		path=npath;
		updateLinkSubscriptions();
	}


	bool eval(){return path.length()>0;}
	float evalFloat(){
		if(datamap) {
			String val=resolveLink(path);;
			float f=val.toFloat();
			return f;
		}
		return 0;
	}
	String evalString(){
		if(datamap) {
			String lval=path;
			if(isLink(lval)) lval=resolveLink(lval);
			return lval;
		}
		return 0;
	}


	void updateLinkSubscriptions(){
		if(!datamap) return;
		for(String p : subpaths)
			datamap->removeListener(p.c_str(),this);
		subpaths.clear();

		String val=datamap->get(path);
		subscribePath(val);
	}
	void subscribePath(String link) {
		if(!datamap) return;
		if(isLink(link)) {
			datamap->on(link.c_str(),this);
			subpaths.push_back(link);
			recurs++;
			if(recurs>MAXLINKRECURS) return;
			subscribePath(datamap->get(link));
			recurs--;

		}
	}

	String resolveLink(String link) { // return the target that is not a link
		if(!datamap) return "";
		if(isLink(link)) {
			String val=datamap->get(link);
			recurs++;
			if(recurs>MAXLINKRECURS) {
				return ""; // loop or too many links
			}
			String res= resolveLink(val);
			recurs--;
			return res;
		} else {return link;}
	};



};




///////////////////////////////////////
class TimeExpression : public Expression {
	Expression *start, *durationon, *durationoff;
public:
	TimeExpression(Expression *startexpr, Expression *durationonexpr, Expression *durationoffexpr){
		start=startexpr;
		durationon=durationonexpr;
		durationoff=durationoffexpr;
		if(start) start->setParent(this);
		if(durationon) durationon->setParent(this);
		if(durationoff) durationoff->setParent(this);
	}

	virtual bool eval(){
		uint32_t nowsec=millis64()/1000;
		uint32_t startts=0,durationonts=0,durationoffts=0;
		if(start){// empty value will be filled by the time expression at first eval
			String startstring=start->evalString();
			if(startstring.length()==0) {
				startts=nowsec;
				// set the var value in the datamap
				// set a ticker now and at every change of the main vars
			}
			else startts=tsFromString(startstring)/1000;
		}
		if(durationon)
			durationonts=durationToMS(durationon->evalString().c_str())/1000;
		if(durationoff)
			durationoffts=durationToMS(durationoff->evalString().c_str())/1000;


		uint32_t diff=nowsec-startts;
		uint32_t total=durationonts+durationoffts;
		uint32_t diffmod=diff%total;

		if(diffmod<=durationonts) return true;

		return false;
	}

	virtual void setDataMap(DataMap *datamap1) {
		datamap=datamap1;
		if(start) start->setDataMap(datamap);
		if(durationon) durationon->setDataMap(datamap);
		if(durationoff) durationoff->setDataMap(datamap);
	}

};



/*
class ConfigJSONLoader: public HeavyJSONLoader {
	DataMap *configmap;
public:
	ConfigJSONLoader (DataMap *configmap0, std::string *json, bool cleanjson0=false, std::string *err0=0):
		HeavyJSONLoader(json,cleanjson0,err0), configmap(configmap0){};
	virtual bool valueCallBack(){
		GenString path=getPath()+stringKey();
		GenString val=stringValue();
		Serial.println(String()+"MyHeavyJSONLoader:"+path.c_str() + ":" + val.c_str() );
		configmap->update(path.c_str(),val.c_str());
		return JSONLoader::valueCallBack();
	};
};

*/

///////////////////////////////////////
class Mapping {

	std::vector<Expression*> expressions;
public:
	std::vector<Expression*> *getExpressions(){return &expressions;};

};

void testMapping(DataMap *datamap,std::vector<Expression*> *vect);
void buildMapping(DataMap *datamap,std::vector<Expression*> *vect);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class MappingManager: public EventListener  {
	Mapping mapping;
	DataMap *datamap;
public:
	void init(DataMap *rdatamap){
		datamap=rdatamap;
		buildMapping(datamap, mapping.getExpressions());
		testMapping(datamap, mapping.getExpressions());
	};
	bool notify(GenString ename, Event *e){return notify(String(ename.c_str()),e);}
	bool notify(String ename, Event *e){
		if(ename=="/set" || ename=="/create"){
			if(ename[ename.length()-1]=='/') ename=ename.substring(0,ename.length()-1);
			StringMapEvent *se=0;
			if(e->isClassType("StringMapEvent")) se=(StringMapEvent *)e;
			if(se==0) return false;
			for(auto it : se->values){
				Serial.println(String()+"MappingManager:notify argument "+it.first.c_str()+":"+it.second.c_str());
				if(datamap->exists(it.first) || ename=="/create") datamap->update(it.first, it.second);
			}
		}
		return false;
	}
};






void testMapping(DataMap *datamap,std::vector<Expression*> *vect) {
	datamap->update("/data/temperature/value","22");

	datamap->update("/data/humidity/value","50");
	//	datamap->update("/data/minhum/threshold","/data/temperature/value");

	datamap->printMap();
	//	datamap->update("/data/temperature/value","80");
	datamap->printMap();
}







void buildMapping(DataMap *datamap,std::vector<Expression*> *vect){
	/*
	// make alarm on less than humidity level
	datamap->set("/data/minhum/threshold","70");
	Ifthenelse *minhumalarm;
	minhumalarm=new Ifthenelse(
			new Comparison("<",new VarExpression("/data/humidity/value"),new VarExpression("/data/minhum/threshold")),
			new Transfer("/data/alarm/active",new VarExpression("1")),
			new Transfer("/data/alarm/active",new VarExpression("0"))
	);
	minhumalarm->setDataMap(datamap);
	vect->push_back(minhumalarm);


	Ifthenelse *maxhumrelay;
	datamap->set("/data/maxhum/threshold","90");
	maxhumrelay=new Ifthenelse(
			new Comparison(">",new VarExpression("/data/humidity/value"), new VarExpression("/data/maxhum/threshold")),
			new Transfer("/data/alarm2/active",new VarExpression("1")),
			new Transfer("/data/alarm2/active",new VarExpression("0"))
	);
	maxhumrelay->setDataMap(datamap);
	vect->push_back(maxhumrelay);
*/
	datamap->set("/timer/start","");	// empty value will be filled by the time expression at first eval
	datamap->set("/timer/onduration","10");	// 10 sec per minute
	datamap->set("/timer/offduration","50");
	Ifthenelse *timerule=new Ifthenelse(
			new TimeExpression(new VarExpression("/data/timer/start"),new VarExpression("/data/timer/onduration"), new VarExpression("/data/timer/offduration")),
			new Transfer("/data/valve/open",new VarExpression("1")),
			new Transfer("/data/valve/open",new VarExpression("0"))
	);
	timerule->setDataMap(datamap);
	timerule->update();	// to make the first eval and set the start time
	vect->push_back(timerule);
}

/*
 *




 *
 * */


#endif
