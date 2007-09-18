/***************************************************************************
 *   Copyright (C) 2007 by Sindre Aam�s                                    *
 *   aamas@stud.ntnu.no                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2 as     *
 *   published by the Free Software Foundation.                            *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License version 2 for more details.                *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   version 2 along with this program; if not, write to the               *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef STR_TO_SDLKEY_H
#define STR_TO_SDLKEY_H

#include <map>
#include <cstring>
#include <SDL.h>

class StrToSdlkey {
	struct StrLess {
		bool operator()(const char *const l, const char *const r) const {
			return std::strcmp(l, r) < 0;
		}
	};

	std::map<const char*,SDLKey,StrLess> m;
	
	void init();
	
public:
	const SDLKey* operator()(const char *str);
};

#endif
