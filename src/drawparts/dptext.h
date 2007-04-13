/***************************************************************************
 *   Copyright (C) 2003-2005 by David Saxton                               *
 *   david@bluehaze.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef CANVASTEXT_H
#define CANVASTEXT_H

#include "drawpart.h"

/**
@short Represents editable text on the canvas
@author David Saxton
*/
class DPText : public DrawPart
{
public:
	DPText( ItemDocument *itemDocument, bool newItem, const char *id = 0 );
	~DPText();

	static Item* construct( ItemDocument *itemDocument, bool newItem, const char *id );
	static LibraryItem *libraryItem();
	static LibraryItem *libraryItemOld();

	virtual void setSelected( bool yes );
	
	virtual QSize minimumSize() const;

protected:
	virtual void postResize();
	
private:
	virtual void drawShape( QPainter &p );
	void dataChanged();
	QString m_caption;
	bool b_displayBackground;
	QColor m_textColor;
	QColor m_backgroundColor;
	QColor m_frameColor;
	RectangularOverlay *m_rectangularOverlay;
};

#endif
