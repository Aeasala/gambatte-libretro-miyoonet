/***************************************************************************
 *   Copyright (C) 2009 by Sindre Aamås                                    *
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
#ifndef CATROM3X_H
#define CATROM3X_H

#include "../videolink.h"
#include "../vfilterinfo.h"

class Catrom3x : public VideoLink {
public:
	enum { OUT_WIDTH = VfilterInfo::IN_WIDTH * 3 };
	enum { OUT_HEIGHT = VfilterInfo::IN_HEIGHT * 3 };
	
	Catrom3x();
	~Catrom3x();
	void draw(void *dst, unsigned dstpitch);
};

#endif
