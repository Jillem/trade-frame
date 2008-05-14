#include "StdAfx.h"
#include "SymbolSelectionFilter.h"

#include "DataManager.h"
using namespace H5;

#include "TimeSeries.h"
#include "HDF5TimeSeriesContainer.h"

#include "RunningStats.h"

#include "Darvas.h"

#include <map>

// destroy self when done
// http://www.codeproject.com/KB/stl/xingstlarticle.aspx  functors

//
// CSymbolSelectionFilter
//
CSymbolSelectionFilter::CSymbolSelectionFilter( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd ) :
  m_DayStartType( dstype) , m_nCount( count ), m_bUseStart( bUseStart ), m_dtStart( dtStart ), m_bUseLast( bUseEnd ), m_dtLast( dtEnd ) {
}

CSymbolSelectionFilter::~CSymbolSelectionFilter(void) {
  m_bars.Clear();
}

// forward reference only
herr_t IterateCallback( hid_t group, const char *name, void *op_data );

// called from IterateCallback (which is called as HDF5 iterates the directory
class CFilterSelectionIteratorControl {
public:
  CFilterSelectionIteratorControl( const string &sBaseGroup, CSymbolSelectionFilter *pFilter ) :
      m_sBaseGroup( sBaseGroup ), m_pFilter( pFilter ) { };
  void Process( const string &sObjectName ) {
    CDataManager dm;
    H5G_stat_t stats;
    string sObjectPath;
    sObjectPath = m_sBaseGroup + sObjectName;
    try {
      dm.GetH5File()->getObjinfo( sObjectPath, stats );
      switch ( stats.type ) {
        case H5G_DATASET: 
          m_pFilter->Process( sObjectName, sObjectPath );
          break;
        case H5G_GROUP:
          int idx = 0;  // starting location for interrupted queries
          sObjectPath.append( "/" );
          CFilterSelectionIteratorControl control( sObjectPath, m_pFilter );  // prepare for recursive call
          int result = dm.GetH5File()->iterateElems( sObjectPath, &idx, &IterateCallback, &control );  
          break;
      }
    }
    catch ( H5::Exception e ) {
      cout << "CFilterSelectionIteratorControl::Process H5::Exception " << e.getDetailMsg() << endl;
      e.walkErrorStack( H5E_WALK_DOWNWARD, (H5E_walk2_t) &CDataManager::PrintH5ErrorStackItem, 0 );
    }
  }
protected:
  string m_sBaseGroup;
  CSymbolSelectionFilter *m_pFilter;
private:
};

void CSymbolSelectionFilter::Start( void ) {
  CDataManager dm;
  int idx = 0;  // starting location for interrupted queries
  string sBaseGroup = "/bar/86400/";
  CFilterSelectionIteratorControl control( sBaseGroup, this );
  int result = dm.GetH5File()->iterateElems( sBaseGroup, &idx, &IterateCallback, &control );
  cout << "iteration returned " << result << endl;
}

herr_t IterateCallback( hid_t group, const char *name, void *op_data ) {
  CFilterSelectionIteratorControl *pControl = 
    ( CFilterSelectionIteratorControl * ) op_data;
  pControl->Process( name );
  return 0;
}

//
// CSelectSymbolWithDarvas
//
class CalcMaxDate: public std::unary_function<CBar &, void> {
public:
  CalcMaxDate( void ) : dtMax( boost::date_time::special_values::min_date_time ), dblMax( 0 ) { };
  void operator() ( CBar &bar ) {
    if ( bar.m_dblClose >= dblMax ) {
      dblMax = bar.m_dblClose;
      dtMax = bar.m_dt;
    }
  }
  operator ptime() { return dtMax; };
protected:
  ptime dtMax;
  double dblMax;
private:
};

// implement boost 1_35_0 soon for the boost::bind

class CalcSixMonMeans {
private:
  CRunningStats rsUpper, rsLower;
  int cnt;
protected:
public:
  CalcSixMonMeans( void ): cnt( 0 ) {};
  ~CalcSixMonMeans( void ) {
    rsUpper.CalcStats();
    rsLower.CalcStats();
    cout << endl << "  upper mean=" << rsUpper.meanY << ", SD=" << rsUpper.SD;
    cout << endl << "  lower mean=" << rsLower.meanY << ", SD=" << rsLower.SD;
  }
  void operator() ( const CBar &bar ) {
    ++cnt;
    double t = cnt;
    rsUpper.Add( t, bar.m_dblHigh - bar.m_dblOpen );
    rsLower.Add( t, bar.m_dblOpen - bar.m_dblLow );
  }
};

class CalcAverageVolume {
private:
  unsigned long m_nTotalVolume;
  unsigned long m_nNumberOfValues;
protected:
public:
  CalcAverageVolume() : m_nTotalVolume( 0 ), m_nNumberOfValues( 0 ) {};
  void operator() ( const CBar &bar ) {
    m_nTotalVolume += bar.m_nVolume;
    ++m_nNumberOfValues;
  }
  operator unsigned long() { return m_nTotalVolume / m_nNumberOfValues; };
};

CSelectSymbolWithDarvas::CSelectSymbolWithDarvas( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd) :
  CSymbolSelectionFilter( dstype, count, bUseStart, dtStart, bUseEnd, dtEnd ) {
}

CSelectSymbolWithDarvas::~CSelectSymbolWithDarvas(void) {
}

bool CSelectSymbolWithDarvas::Validate( void ) {
  return ( CSymbolSelectionFilter::Validate() && m_bUseStart && m_bUseLast );
}

void CSelectSymbolWithDarvas::Process( const string &sSymbol, const string &sPath ) {
  //cout << "Darvas for " << sSymbol << ", " << m_bars.Count() << " bars." << endl;
  CHDF5TimeSeriesContainer<CBar> barRepository( sPath );
  CHDF5TimeSeriesContainer<CBar>::iterator begin, end;
  ptime dtPt1 = m_dtStart - m_dtStart.time_of_day();  // normalize day of max
  ptime dtPt2 = m_dtLast - m_dtLast.time_of_day();  // normalize end day ( 1 past day of last bar )
  ptime dtOneYrAgo = dtPt2 - date_duration(52 * 7); // get 52 weeks of values from day of max value
  ptime dt26WksAgo = dtPt2 - date_duration(26 * 7);
  ptime dtTrigger = dtPt2 - date_duration( 1 );
  begin = lower_bound( barRepository.begin(), barRepository.end(), dtOneYrAgo );
  end = lower_bound( begin, barRepository.end(), dtPt2 );  // retrieve to one day past trigger
  hsize_t cnt = end - begin;
  if ( ( 240 < cnt ) && ( (*(end-1)).m_dt == dtTrigger ) ){   // at least 240 bars, with required last bar
    m_bars.Resize( cnt );
    barRepository.Read( begin, end, &m_bars );

    vector<CBar>::iterator volIter = m_bars.end() - 20;
    unsigned long nAverageVolume = std::for_each( volIter, m_bars.end(), CalcAverageVolume() );

    if ( 1000000 < nAverageVolume ) {  // need certain amount of liquidity before entering trade (20 bars worth)

      ptime dtDayOfMax = std::for_each( m_bars.begin(), m_bars.end(), CalcMaxDate() );
      if ( dtDayOfMax >= dtPt1 ) {
        cout << 
          "Darvas max for " << sSymbol << 
          " on " << dtDayOfMax << 
          ", close=" << m_bars.Last()->m_dblClose <<
          ", volume=" << m_bars.Last()->m_nVolume;
        vector<CBar>::iterator iter = m_bars.iterAtOrAfter( dtPt2 - date_duration( 20 ) ); // take 20 days to run trigger
        CDarvasResults results = for_each( iter, m_bars.end(), CDarvas() );
        if ( results.GetTrigger() ) {
          cout << " triggered, stop=" << results.GetStopLevel();
          iter = m_bars.iterAtOrAfter( dt26WksAgo );
          for_each( iter, m_bars.end(), CalcSixMonMeans() );
        }
        cout << endl;
      }
    }
  }
}

//
// CSelectSymbolWith10Percent
//
CSelectSymbolWith10Percent::CSelectSymbolWith10Percent( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd) :
  CSymbolSelectionFilter( dstype, count, bUseStart, dtStart, bUseEnd, dtEnd ) {
}

CSelectSymbolWith10Percent::~CSelectSymbolWith10Percent(void) {
}

bool CSelectSymbolWith10Percent::Validate( void ) {
  return ( CSymbolSelectionFilter::Validate() && m_bUseLast );
}

void CSelectSymbolWith10Percent::Process( const string &sSymbol, const string &sPath ) {
  //cout << "10 Percent for " << sSymbol << ", " << m_bars.Count() << " bars." << endl;
  CHDF5TimeSeriesContainer<CBar> barRepository( sPath );
  CHDF5TimeSeriesContainer<CBar>::iterator begin, end;
  ptime dtPt2 = m_dtLast - m_dtLast.time_of_day();  // normalize end day ( 1 past day of last bar )
  end = lower_bound( barRepository.begin(), barRepository.end(), dtPt2 );
  //if (  ( end != barRepository.end() ) ) {
    if ( 20 < ( end - barRepository.begin() ) ) {  // make sure there are at least 20 bars available
      begin = end - 20;
      m_bars.Resize( 20 );
      barRepository.Read( begin, end, &m_bars );
      if ( m_bars.Last()->m_dt == ( dtPt2 - date_duration( 1 ) ) ) {
        unsigned long nAverageVolume = std::for_each( m_bars.begin(), m_bars.end(), CalcAverageVolume() );
        if ( 1000000 < nAverageVolume ) {  // need certain amount of liquidity before entering trade (20 bars worth)
          multimap<double, string>::iterator iterPos;
          std::multimap<double, string, MaxNegativesCompare>::iterator iterNeg;
          double dblReturn = ( m_bars.Last()->m_dblClose - m_bars.Last()->m_dblOpen ) / m_bars.Last()->m_dblClose;
          if ( nMaxInList > mapMaxPositives.size() ) {
            mapMaxPositives.insert( pair<double, string>( dblReturn, sSymbol ) );
          }
          else {
            iterPos = mapMaxPositives.begin();
            if ( dblReturn > iterPos->first ) {
              mapMaxPositives.erase( iterPos );
              mapMaxPositives.insert( pair<double, string>( dblReturn, sSymbol ) );
            }
          }
          if ( nMaxInList > mapMaxNegatives.size() ) {
            mapMaxNegatives.insert( pair<double, string>( dblReturn, sSymbol ) );
          }
          else {
            iterNeg = mapMaxNegatives.begin();
            if ( dblReturn < iterNeg->first ) {
              mapMaxNegatives.erase( iterNeg );
              mapMaxNegatives.insert( pair<double, string>( dblReturn, sSymbol ) );
            }
          }
        }
      }
    }
  //}
}

void CSelectSymbolWith10Percent::WrapUp( void ) {
  cout << "Positives: " << endl;
  multimap<double, string>::iterator iterPos;
  for ( iterPos = mapMaxPositives.begin(); iterPos != mapMaxPositives.end(); ++iterPos ) {
    cout << " " << iterPos->second << "=" << iterPos->first << endl;
  }
  mapMaxPositives.clear();
  cout << "Negatives: " << endl;
  std::multimap<double, string, MaxNegativesCompare>::iterator iterNeg;
  for ( iterNeg = mapMaxNegatives.begin(); iterNeg != mapMaxNegatives.end(); ++iterNeg ) {
    cout << " " << iterNeg->second << "=" << iterNeg->first << endl;
  }
  mapMaxNegatives.clear();
}

//
// CSelectSymbolWithBollinger
//
CSelectSymbolWithBollinger::CSelectSymbolWithBollinger( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd) :
  CSymbolSelectionFilter( dstype, count, bUseStart, dtStart, bUseEnd, dtEnd ) {
}

CSelectSymbolWithBollinger::~CSelectSymbolWithBollinger(void) {
}

void CSelectSymbolWithBollinger::Process( const string &sSymbol, const string &sPath ) {
  cout << "Bollinger for " << sSymbol << ", " << m_bars.Count() << " bars." << endl;
  CHDF5TimeSeriesContainer<CBar> barRepository( sPath );
  CHDF5TimeSeriesContainer<CBar>::iterator begin, end;
  end = lower_bound( barRepository.begin(), barRepository.end(), m_dtStart );
  switch ( m_DayStartType ) {
    case DaySelect:
      begin = lower_bound( barRepository.begin(), end, m_dtStart );
      break;
    case BarCount:
      break;
      begin = end;
      begin -= m_nCount;
    case DayCount:
      date_duration tmp( m_nCount );
      ptime dt = (*end).m_dt - tmp;
      begin = lower_bound( barRepository.begin(), end, dt );
  }
  hsize_t cnt = end - begin;
  //CBars bars( cnt );
  void *p = m_bars.First();
  int p2 = sizeof( CBar );
  m_bars.Resize( cnt );
  barRepository.Read( begin, end, &m_bars );
}

//
// CSelectSymbolWithVolatility
//
CSelectSymbolWithVolatility::CSelectSymbolWithVolatility( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd) :
  CSymbolSelectionFilter( dstype, count, bUseStart, dtStart, bUseEnd, dtEnd ) {
}

CSelectSymbolWithVolatility::~CSelectSymbolWithVolatility(void) {
}

bool CSelectSymbolWithVolatility::Validate( void ) {
  return ( CSymbolSelectionFilter::Validate() && m_bUseLast );
}

class CalcAverageVolatility {
private:
  double m_dblVolatility;
  unsigned long m_nNumberOfValues;
protected:
public:
  CalcAverageVolatility() : m_dblVolatility( 0 ), m_nNumberOfValues( 0 ) {};
  void operator() ( const CBar &bar ) {
    //m_dblVolatility += ( bar.m_dblHigh - bar.m_dblLow ) / bar.m_dblClose;
    m_dblVolatility += ( bar.m_dblHigh - bar.m_dblLow ) ;
    ++m_nNumberOfValues;
  }
  operator double() { return m_dblVolatility / m_nNumberOfValues; };
};

void CSelectSymbolWithVolatility::Process( const string &sSymbol, const string &sPath ) {
  //cout << "Volatility for " << sSymbol << ", " << m_bars.Count() << " bars." << endl;
  CHDF5TimeSeriesContainer<CBar> barRepository( sPath );
  CHDF5TimeSeriesContainer<CBar>::iterator begin, end;
  ptime dtPt2 = m_dtLast - m_dtLast.time_of_day();  // normalize end day ( 1 past day of last bar )
  end = lower_bound( barRepository.begin(), barRepository.end(), dtPt2 );
  if ( 20 < ( end - barRepository.begin() ) ) {  // make sure there are at least 20 bars available
    begin = end - 20;
    m_bars.Resize( 20 );
    barRepository.Read( begin, end, &m_bars );
    if ( ( m_bars.Last()->m_dblClose < 60.0 ) && ( m_bars.Last()->m_dt == ( dtPt2 - date_duration( 1 ) ) ) ) {
      unsigned long nAverageVolume = std::for_each( m_bars.begin(), m_bars.end(), CalcAverageVolume() );
      if ( 1000000 < nAverageVolume ) {  // need certain amount of liquidity before entering trade (20 bars worth)
        double dblAverageVolatility = std::for_each( m_bars.begin(), m_bars.end(), CalcAverageVolatility() );
        //double dblVolatility = ( m_bars.Last()->m_dblHigh - m_bars.Last()->m_dblLow ) / m_bars.Last()->m_dblClose;
        multimap<double, string>::iterator iterPos;
        if ( nMaxInList > mapMaxVolatility.size() ) {
          mapMaxVolatility.insert( pair<double, string>( dblAverageVolatility, sSymbol ) );
        }
        else {
          iterPos = mapMaxVolatility.begin();
          if ( dblAverageVolatility > iterPos->first ) {
            mapMaxVolatility.erase( iterPos );
            mapMaxVolatility.insert( pair<double, string>( dblAverageVolatility, sSymbol ) );
          }
        }
      }
    }
  }
}

void CSelectSymbolWithVolatility::WrapUp( void ) {
  cout << "Volatiles: " << endl;
  multimap<double, string>::iterator iterPos;
  for ( iterPos = mapMaxVolatility.begin(); iterPos != mapMaxVolatility.end(); ++iterPos ) {
    cout << " " << iterPos->second << "=" << iterPos->first << endl;
  }
  mapMaxVolatility.clear();
}

//
// CSelectSymbolWithBreakout
//
CSelectSymbolWithBreakout::CSelectSymbolWithBreakout( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd) :
  CSymbolSelectionFilter( dstype, count, bUseStart, dtStart, bUseEnd, dtEnd ) {
}

CSelectSymbolWithBreakout::~CSelectSymbolWithBreakout(void) {
}

void CSelectSymbolWithBreakout::Process( const string &sSymbol, const string &sPath ) {
  cout << "Breakout for " << sSymbol << ", " << m_bars.Count() << " bars." << endl;
}


//
// CSelectSymbolWithXWeekHigh
//
CSelectSymbolWithXWeekHigh::CSelectSymbolWithXWeekHigh( enumDayCalc dstype, int count, bool bUseStart, ptime dtStart, bool bUseEnd, ptime dtEnd) :
  CSymbolSelectionFilter( dstype, count, bUseStart, dtStart, bUseEnd, dtEnd ) {
}

CSelectSymbolWithXWeekHigh::~CSelectSymbolWithXWeekHigh(void) {
}

void CSelectSymbolWithXWeekHigh::Process( const string &sSymbol, const string &sPath ) {
  //cout << "CSelectSymbolWithXWeekHigh for " << sSymbol << ", " << m_bars.Count() << " bars." << endl;
  CHDF5TimeSeriesContainer<CBar> barRepository( sPath );
  CHDF5TimeSeriesContainer<CBar>::iterator begin, end;
  ptime dtStart = m_dtStart - date_duration(52 * 7); // get 52 weeks of values from day of max value
  begin = lower_bound( barRepository.begin(), barRepository.end(), dtStart );
  //end = lower_bound( begin, barRepository.end(), m_dtLast );  // retrieve to end for the trigger
  end = barRepository.end();
  hsize_t cnt = end - begin;
  if ( 240 < cnt ) {   // at least 200 bars needed
    m_bars.Resize( cnt );
    barRepository.Read( begin, end, &m_bars );
    ptime dtDayOfMax = std::for_each( m_bars.begin(), m_bars.end(), CalcMaxDate() );
    ptime dtDayRequested = m_dtStart - m_dtStart.time_of_day();  // normalize to midnight
    if ( dtDayOfMax == dtDayRequested ) {
      cout << "XDayMax has max for " << sSymbol << " on " << dtDayOfMax << endl;
      vector<CBar>::iterator iter = m_bars.iterAtOrAfter( dtDayRequested - date_duration( 20 ) ); // take 20 days to run trigger
    }
  }
}

