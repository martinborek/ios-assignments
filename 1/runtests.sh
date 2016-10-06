#!/bin/bash

export LC_ALL=C

paramv=false
paramt=false
paramr=false
params=false
paramc=false
paramhelp=true # ma-li se vypsat chyba

vychozipwd=$PWD
errcode=0 # chybovy status inicializovan na 0 ~ OK

while getopts :vtrsc param
do
    case "$param" in
    v)  paramv=true; paramhelp=false;;
    t)  paramt=true; paramhelp=false;;
    r)  paramr=true; paramhelp=false;;
    s)  params=true; paramhelp=false;;
    c)  paramc=true; paramhelp=false;;
    *)  paramhelp=true;;
    esac
done

# malo parametru
if [ $# -eq 1 ]; then
  paramhelp=true
fi

shift $(($OPTIND - 1))

if [ -z $1 ] || [ $3 ]; then
  paramhelp=true
fi

# vypis napovedy, pokud byly spatne zadane parametry
if [ $paramhelp = true ]; then
  printf "Usage: $0 [-vtrsc] TEST_DIR [REGEX]
   
    -v  validate tree
    -t  run tests
    -r  report results
    -s  synchronize expected results
    -c  clear generated files

    It is mandatory to supply at least one option.\n" >&2
  exit 2
fi

TEXT_DIR="$1"

if [ "$2" ]; then
  REGEX="$2"
fi

# neexistujici adresar
if [ ! -d "$TEXT_DIR" ]; then
  echo "Zadany adresar neexistuje" >&2
  exit 2
fi

# presun do zadaneho adresare
cd "$TEXT_DIR" || {
  echo "Nelze se presunout" >&2
  exit 2
}
slozkapwd="$PWD"

if [ $paramv = true ]; then
# najde vsechny adresare
for adresar in `eval find . -type d ${REGEX+| grep -E '$REGEX' }| sort`;
do
  if [ `find "$adresar" -mindepth 1 -maxdepth 1 -type d | wc -w` -ne 0 ]; then # existuje v danem adresari adresar?
    if [ `find "$adresar" -maxdepth 1 -type f | wc -w` -ne 0 ]; then # pokud ano a existuje i soubor -> varovani
      errcode=1
      echo Slozky a soubory v jednom adresari: "$adresar" >&2
    fi
  else # neobsahuje adresar, musi zkontrolovat, zda existuje cmd-given a je spustitelny
    if [ ! -x "$adresar"/cmd-given ]; then
      errcode=1
      echo Neni zde spustitelny cmd-given: "$adresar" >&2
    fi
  fi

  for symlink in `eval find "$adresar" -maxdepth 1 -type l | sort`; # hleda symlinky
  do
    echo Nalezen symlink: "$symlink" >&2
    errcode=1
  done

  for hardlink in `eval find "$adresar" -maxdepth 1 -type f -links +1 `; # hleda vicenasobne pevne odkazy
  do
    echo Nalezen mnohonasobny pevny odkaz: "$hardlink" >&2
    errcode=1
  done

  # kontrola, zda soubor stdin-given ma prava pro cteni
  if [ -f "$adresar"/stdin-given -a ! -r "$adresar"/stdin-given ]; then
    echo Soubor stdin-given neni pristupny pro cteni: "$adresar" >&2
    errcode=1
  fi

  # kontrola, zda maji urcene soubory prava pro zapis
  for soubor in `eval find "$adresar" -maxdepth 1 -type f | grep -E '/(stdout|stderr|status)-(expected|captured|delta)$' | sort`;
  do
    if [ ! -w "$soubor" ]; then
      echo Soubor neni pristupny pro zapis: "$soubor" >&2
      errcode=1
    fi
    
    # kontrola, zda dane soubory obsahuji jen cele cislo + konec radku
    if [ `basename "$soubor"` = "status-expected" -o `basename "$soubor"` = "status-captured" ]; then
      if [ -z "`cat "$soubor" | grep -E '^[0-9]+$'`" ]; then
        echo Soubor neobsahuje cele cislo: "$soubor" >&2
	errcode=1
      fi
      if [ `wc -l < "$soubor"` -ne 1 ]; then
        echo Soubor nema prave jeden radek: "$soubor" >&2
	errcode=1
      fi
    fi
  done

  # kontrola na existenci nespecifikovaneho souboru
  for neznamysoubor in `eval find "$adresar" -maxdepth 1 -type f | grep -E -v '(/|^)((stdout|stderr|status)-(expected|captured|delta)|stdin-given|cmd-given)$' | sort`;
  do
    echo Ve stromu se vyskytuje neznamy soubor: "$neznamysoubor" >&2
    errcode=1
  done

done
fi

# operace -t
if [ $paramt = true ]; then
  for adresar in `eval find . -name cmd-given -type f ${REGEX+| grep -E '$REGEX' }| sort`;
  do
    # presunuti do aktualne zpracovavane slozky
    cd "$slozkapwd" || {
      echo "Nelze se presunout" >&2
      exit 2
    }
    adresar=`dirname "$adresar"`
    cd "$adresar" || {
      echo "Nelze se presunout" >&2
      exit 2
    }

    # pokud existuje stdin-given, jde na vstup
    stdin=stdin-given
    if [ ! -f "stdin-given" ]; then
      stdin=/dev/null
    fi
    ./cmd-given < "$stdin"  1>"stdout-captured" 2>"stderr-captured"

    echo $? >"status-captured" # zapis stavu provedeneho skriptu
     
    for zapis in stdout stderr status;
    do # porovnavani obsahu souboru
      diff "$zapis"-expected "$zapis"-captured >"$zapis"-delta || {
        errcode=1
      }
    done

    result=OK # nastaveni pocatecni hodnoty
    for kontrola in stdout stderr status;
    do # kontroluje se, zda jsou testy OK
      if [ `wc -c < "$kontrola"-delta` -ne 0 ]; then
        result=FAILED
      fi
    done
    
    if [ $result != OK ]; then
      errcode=1 
    fi
    
    # barevny vypis OK a FAILED
    if [ -t 2 ]; then
      if [ $result = OK ]; then
        result="\033[32mOK\033[0m"
      else
        result="\033[31mFAILED\033[0m"
      fi
    fi
    
    adresar=`echo "$adresar" | sed -e "s/^.\///"` # uprava na kanonicky tvar
    echo -e "$adresar: $result" >&2 # vypis vysledku testu
 
  done

  #navrat do adresare
  cd "$slozkapwd" || {
    echo "Nelze se presunout" >&2
    exit 2
  }

fi

# operace -r
if [ $paramr = true ]; then

  for adresar in `eval find . -name cmd-given -type f ${REGEX+| grep -E '$REGEX' }| sort`;
  do

    cd "$slozkapwd" || {
      echo "Nelze se presunout" >&2
      exit 2
    }

    adresar=`dirname $adresar`
    cd "$adresar" || {
      echo "Nelze se presunout" >&2
      exit 2
    }

    for zapis in stdout stderr status;
    do # porovnavani obsahu souboru
      diff "$zapis"-expected "$zapis"-captured >"$zapis"-delta || {
        errcode=1
      }
    done

    result=OK
    for kontrola in stdout stderr status;
    do # kontroluje se, zda jsou testy OK
      if [ `wc -c < "$kontrola"-delta` -ne 0 ]; then
        result=FAILED
      fi
    done
    
    if [ $result != OK ]; then
      errcode=1  
    fi
    # barevny vypis OK a FAILED    
    if [ -t 1 ]; then
      if [ $result = OK ]; then
        result="\033[32mOK\033[0m"
      else
        result="\033[31mFAILED\033[0m"
      fi
    fi

    adresar=`echo $adresar | sed -e "s/^.\///"`

    echo -e "$adresar: $result" # vypis vysledku testu
 
  done

  #navrat do adresare
  cd "$slozkapwd" || {
    echo "Nelze se presunout" >&2
    exit 2
  }

fi

# operace -s
if [ $params = true ]; then

  for adresar in `eval find . -type d ${REGEX+| grep -E '$REGEX' }| sort`;
  do # prochazeni vybranych adresaru
    for file in `eval find "$adresar" -maxdepth 1 -type f | grep -E '(/|^)(stdout|stderr|status)-captured$'`; 
    do # premistovani souboru
      mv "$file" `echo "$file" | sed -e "s/captured$/expected/"` || {
        echo "Nelze premistit soubor" >&2
        errcode=1
      }
    done

  done

fi

# operace -c
if [ $paramc = true ]; then

  for adresar in `eval find . -type d ${REGEX+| grep -E '$REGEX' }| sort`;
  do # prochazeni vybranych adresaru
    for file in `eval find "$adresar" -maxdepth 1 -type f | grep -E '(/|^)(stdout|stderr|status)-(captured|delta)$'`; 
    do # mazani souboru
      rm "$file" || {
        echo "Nelze smazat soubor" >&2
        errcode=1
      }
    done

  done

fi

cd "$vychozipwd" || {
  echo "Nelze se presunout do vychoziho adresare" >&2
  exit 2
}

exit $errcode
