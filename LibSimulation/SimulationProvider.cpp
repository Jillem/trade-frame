/************************************************************************
 * Copyright(c) 2009, One Unified. All rights reserved.                 *
 *                                                                      *
 * This file is provided as is WITHOUT ANY WARRANTY                     *
 *  without even the implied warranty of                                *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                *
 *                                                                      *
 * This software may not be used nor distributed without proper license *
 * agreement.                                                           *
 *                                                                      *
 * See the file LICENSE.txt for redistribution information.             *
 ************************************************************************/

#include "StdAfx.h"

#include <stdexcept>
#include <cassert>

#include "LibHDF5TimeSeries/HDF5DataManager.h"

#include "SimulationProvider.h"

CSimulationProvider::CSimulationProvider(void)
: CProviderInterface<CSimulationProvider,CSimulationSymbol>(), 
  m_pMerge( NULL )
{
  m_sName = "Simulator";
  m_nID = EProviderSimulator;

}

CSimulationProvider::~CSimulationProvider(void) {

  if ( NULL != m_pMerge ) {
    delete m_pMerge;
    m_pMerge = NULL;
  }
}

void CSimulationProvider::SetGroupDirectory( const std::string sGroupDirectory ) {
  CHDF5DataManager dm;
  std::string s;
  if( !dm.GroupExists( sGroupDirectory ) ) throw std::invalid_argument( "Could not find: " + sGroupDirectory );
  s = sGroupDirectory + "/trades";
  if( !dm.GroupExists( s ) ) throw std::invalid_argument( "Could not find: " + s );
  s = sGroupDirectory + "/quotes";
  if( !dm.GroupExists( s ) ) throw std::invalid_argument( "Could not find: " + s );
  m_sGroupDirectory = sGroupDirectory;
}

void CSimulationProvider::Connect() {
  if ( !m_bConnected ) {
    m_bConnected = true;
    CProviderInterface::Connect();
    OnConnected( 0 );
  }
}

void CSimulationProvider::Disconnect() {
  if ( m_bConnected ) {
    m_bConnected = false;
    CProviderInterface::Disconnect();
    OnDisconnected( 0 );
  }
}

CSimulationProvider::pSymbol_t CSimulationProvider::NewCSymbol( CSimulationSymbol::pInstrument_t pInstrument ) {
  pSymbol_t pSymbol( new CSimulationSymbol(pInstrument->GetInstrumentName(), pInstrument, m_sGroupDirectory) );
  pSymbol->m_simExec.SetOnOrderFill( MakeDelegate( this, &CSimulationProvider::HandleExecution ) );
  //CProviderInterface::AddTradeHandler( sSymbolName, MakeDelegate( &pSymbol->m_simExec, &CSimulateOrderExecution::NewTrade ) );
  inherited_t::AddCSymbol( pSymbol );
  return pSymbol;
}

void CSimulationProvider::AddTradeHandler( const std::string &sSymbol, CSimulationSymbol::tradehandler_t handler ) {
  CProviderInterface<CSimulationProvider,CSimulationSymbol>::AddTradeHandler( sSymbol, handler );
  CProviderInterface<CSimulationProvider,CSimulationSymbol>::m_mapSymbols_t::iterator iter;
  iter = m_mapSymbols.find( sSymbol );
  assert( m_mapSymbols.end() != iter );
  pSymbol_t pSymSymbol( iter->second );
  if ( 1 == iter->second->GetTradeHandlerCount() ) {
    inherited_t::AddTradeHandler( sSymbol, MakeDelegate( &pSymSymbol->m_simExec, &CSimulateOrderExecution::NewTrade ) );
  }
}

void CSimulationProvider::RemoveTradeHandler( const std::string &sSymbol, CSimulationSymbol::tradehandler_t handler ) {
  inherited_t::RemoveTradeHandler( sSymbol, handler );
  inherited_t::m_mapSymbols_t::iterator iter;
  iter = m_mapSymbols.find( sSymbol );
  if ( m_mapSymbols.end() == iter ) {
    assert( false );  // this shouldn't occur
  }
  else {
    if ( 1 == iter->second->GetTradeHandlerCount() ) {
      pSymbol_t pSymSymbol( iter->second );
      inherited_t::RemoveTradeHandler( sSymbol, MakeDelegate( &pSymSymbol->m_simExec, &CSimulateOrderExecution::NewTrade ) );
    }
  }
}

// these need to open the data file, load the data, and prepare to simulate
void CSimulationProvider::StartQuoteWatch( pSymbol_t pSymbol ) {
  pSymbol->StartQuoteWatch();
}

void CSimulationProvider::StopQuoteWatch( pSymbol_t pSymbol ) {
  pSymbol->StopQuoteWatch();
}

void CSimulationProvider::StartTradeWatch( pSymbol_t pSymbol ) {
  pSymbol->StartTradeWatch();
}

void CSimulationProvider::StopTradeWatch( pSymbol_t pSymbol ) {
  pSymbol->StopTradeWatch();
}

void CSimulationProvider::StartDepthWatch( pSymbol_t pSymbol ) {
  pSymbol->StartDepthWatch();
}

void CSimulationProvider::StopDepthWatch( pSymbol_t pSymbol ) {
  pSymbol->StopDepthWatch();
}

//void CSimulationProvider::Merge( LPVOID lpParam ) {
//  CSimulationProvider *pProvider = reinterpret_cast<CSimulationProvider *>( lpParam );
void CSimulationProvider::Merge( void ) {

  // for each of the symbols, add the quote and trade series
  // datums from each series will be merged and emittedin in chronological order
  for ( m_mapSymbols_t::iterator iter = m_mapSymbols.begin();
    iter != m_mapSymbols.end(); ++iter ) {
      pSymbol_t sym( iter->second );
      CQuotes* quotes = &sym->m_quotes;
      m_pMerge -> Add( 
        quotes, 
        MakeDelegate( iter->second.get(), &CSimulationSymbol::HandleQuoteEvent ) );
      CTrades *trades = &sym->m_trades;
      m_pMerge -> Add( 
        trades, 
        MakeDelegate( iter->second.get(), &CSimulationSymbol::HandleTradeEvent ) );
  }

  bool bOldMode = CTimeSource::Instance().GetSimulationMode();
  m_dtSimStart = CTimeSource::Instance().External();
  CTimeSource::Instance().SetSimulationMode();
  m_pMerge -> Run();
  CTimeSource::Instance().SetSimulationMode( bOldMode );
  m_dtSimStop = CTimeSource::Instance().External();

  // use event here instead
//  m_pMergeThread = NULL;
  // use join to determine completion?  or state machine?
  // be careful, as it may deadlock
}

void CSimulationProvider::Run() {
  if ( 0 == m_sGroupDirectory.size() ) throw std::invalid_argument( "Group Directory is empty" );
  if ( 0 == m_mapSymbols.size() ) throw std::invalid_argument( "No Symbols to simulate" );
  // how to detect end of thread, and reset m_hMergeThread?
  if ( NULL != m_pMerge ) {
    std::cout << "Simulation already in progress" << std::endl;
  }
  else {
    m_pMerge = new CCrossThreadMerge();
    m_threadMerge = boost::thread( boost::bind( &CSimulationProvider::Merge, this ) );
    //m_pMergeThread = AfxBeginThread( &CSimulationProvider::Merge, reinterpret_cast<LPVOID>( this ) );
//    assert( NULL != m_pMergeThread );
  }
}

// at some point:  run, stop, pause, resume, reset
void CSimulationProvider::Stop() {
  if ( NULL == m_pMerge ) {
    std::cout << "no simulation to stop" << std::endl;
  }
  else {
    m_pMerge->Stop();
    std::cout << "stopping simulation" << std::endl;
  }
}

void CSimulationProvider::PlaceOrder( pOrder_t pOrder ) {
  inherited_t::PlaceOrder( pOrder ); // any underlying initialization
  m_mapSymbols_t::iterator iter = m_mapSymbols.find( pOrder->GetInstrument()->GetInstrumentName() );
  if ( m_mapSymbols.end() == iter ) {
    std::cout << "Can't place order, can't find symbol: " << pOrder->GetInstrument()->GetInstrumentName() << std::endl;
  }
  else {
    iter->second->m_simExec.SubmitOrder( pOrder );
  }
}

void CSimulationProvider::CancelOrder( pOrder_t pOrder ) {
  inherited_t::CancelOrder( pOrder );
  m_mapSymbols_t::iterator iter = m_mapSymbols.find( pOrder->GetInstrument()->GetInstrumentName() );
  if ( m_mapSymbols.end() == iter ) {
    std::cout << "Can't cancel order, can't find symbol: " << pOrder->GetInstrument()->GetInstrumentName() << std::endl;
  }
  else {
    iter->second->m_simExec.CancelOrder( pOrder->GetOrderId() );
  }
}

void CSimulationProvider::HandleExecution( COrder::orderid_t orderId, const CExecution &exec ) {
  COrderManager::Instance().ReportExecution( orderId, exec );
}

