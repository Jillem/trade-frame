/************************************************************************
 * Copyright(c) 2012, One Unified. All rights reserved.                 *
 * email: info@oneunified.net                                           *
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

#pragma once

// Started 2012/10/14

#include "InMemoryMktSymbolList.h"

namespace ou { // One Unified
namespace tf { // TradeFrame
namespace iqfeed { // IQFeed

namespace MktSymbolLoadType {
  enum Enum { Download, LoadTextFromDisk };
}

typedef InMemoryMktSymbolList symbols_t;

void LoadMktSymbols( symbols_t& symbols, MktSymbolLoadType::Enum, bool bSaveTextToDisk );

} // namespace iqfeed
} // namespace tf
} // namespace ou