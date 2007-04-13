/***************************************************************************
 *   Copyright (C) 2003-2005 by David Saxton                               *
 *   david@bluehaze.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "canvasmanipulator.h"
#include "circuitdocument.h"
#include "circuitview.h"
#include "component.h"
#include "connector.h"
#include "core/ktlconfig.h"
#include "cnitemgroup.h"
#include "documentiface.h"
#include "drawpart.h"
#include "ecnode.h"
#include "itemdocumentdata.h"
#include "ktechlab.h"
#include "pin.h"
#include "simulator.h"
#include "subcircuits.h"
#include "switch.h"

#include <kdebug.h>
#include <kinputdialog.h> 
#include <klocale.h>
#include <kmessagebox.h>
#include <qregexp.h>
#include <qtimer.h>
#include <cassert>

CircuitDocument::CircuitDocument( const QString & caption, KTechlab *ktechlab, const char *name )
	: ICNDocument( caption, ktechlab, name )
{
	m_pOrientationAction = new KActionMenu( i18n("Orientation"), "rotate", this );
	
	m_type = Document::dt_circuit;
	m_pDocumentIface = new CircuitDocumentIface(this);
	m_fileExtensionInfo = i18n("*.circuit|Circuit(*.circuit)\n*|All Files");
	
	m_cmManager->addManipulatorInfo( CMSelect::manipulatorInfo() );
	m_cmManager->addManipulatorInfo( CMDraw::manipulatorInfo() );
	m_cmManager->addManipulatorInfo( CMRightClick::manipulatorInfo() );
	m_cmManager->addManipulatorInfo( CMRepeatedItemAdd::manipulatorInfo() );
	m_cmManager->addManipulatorInfo( CMItemResize::manipulatorInfo() );
	m_cmManager->addManipulatorInfo( CMItemDrag::manipulatorInfo() );
	
	connect( this, SIGNAL(connectorAdded(Connector*)), this, SLOT(requestAssignCircuits()) );
	connect( this, SIGNAL(connectorAdded(Connector*)), this, SLOT(connectorAdded(Connector*)) );
	
	m_updateCircuitsTmr = new QTimer();
	connect( m_updateCircuitsTmr, SIGNAL(timeout()), this, SLOT(assignCircuits()) );
	
	requestStateSave();
}

CircuitDocument::~CircuitDocument()
{
	m_bDeleted = true;
	deleteCircuits();
	
	delete m_updateCircuitsTmr;
	delete m_pDocumentIface;
}

void CircuitDocument::slotInitItemActions( Item *itemBase )
{
	ICNDocument::slotInitItemActions(itemBase);
	
	CircuitView * activeCircuitView = dynamic_cast<CircuitView*>(activeView());
	if ( !p_ktechlab || !activeCircuitView )
		return;
	
	Component * item = dynamic_cast<Component*>(itemBase);
	
	if ( !item && m_selectList->count() > 0 || !m_selectList->itemsAreSameType() )
		return;
	
	KAction * orientation_actions[] = {
		activeCircuitView->action("edit_orientation_0"),
		activeCircuitView->action("edit_orientation_90"),
		activeCircuitView->action("edit_orientation_180"),
		activeCircuitView->action("edit_orientation_270") };

	if ( !item || !item->canRotate() ) {
		for ( unsigned i = 0; i < 4; ++i )
			orientation_actions[i]->setEnabled(false);
		return;
	}

	for ( unsigned i = 0; i < 4; ++ i) {
		orientation_actions[i]->setEnabled(true);
		m_pOrientationAction->remove( orientation_actions[i] );
		m_pOrientationAction->insert( orientation_actions[i] );
	}

	if ( item->angleDegrees() == 0 )
		(static_cast<KToggleAction*>( orientation_actions[0] ))->setChecked(true);
	else if ( item->angleDegrees() == 90 )
		(static_cast<KToggleAction*>( orientation_actions[1] ))->setChecked(true);
	else if ( item->angleDegrees() == 180 )
		(static_cast<KToggleAction*>( orientation_actions[2] ))->setChecked(true);
	else if ( item->angleDegrees() == 270 )
		(static_cast<KToggleAction*>( orientation_actions[3] ))->setChecked(true);
}

void CircuitDocument::rotateCounterClockwise()
{
	m_selectList->slotRotateCCW();
	requestRerouteInvalidatedConnectors();
}

void CircuitDocument::rotateClockwise()
{
	m_selectList->slotRotateCW();
	requestRerouteInvalidatedConnectors();
}

void CircuitDocument::itemFlip()
{
	m_selectList->slotFlip();
	requestRerouteInvalidatedConnectors();
}

void CircuitDocument::setOrientation0()
{
	m_selectList->slotSetOrientation0();
	requestRerouteInvalidatedConnectors();
}

void CircuitDocument::setOrientation90()
{
	m_selectList->slotSetOrientation90();
	requestRerouteInvalidatedConnectors();
}

void CircuitDocument::setOrientation180()
{
	m_selectList->slotSetOrientation180();
	requestRerouteInvalidatedConnectors();
}

void CircuitDocument::setOrientation270()
{
	m_selectList->slotSetOrientation270();
	requestRerouteInvalidatedConnectors();
}

View *CircuitDocument::createView( ViewContainer *viewContainer, uint viewAreaId, const char *name )
{
	View *view = new CircuitView( this, viewContainer, viewAreaId, name );
	handleNewView(view);
	return view;
}

void CircuitDocument::slotUpdateConfiguration()
{
	ICNDocument::slotUpdateConfiguration();

	NodeList::iterator nodeEnd = m_nodeList.end();
	for(NodeList::iterator it = m_nodeList.begin(); it != nodeEnd; ++it) {
		(static_cast<ECNode*>((Node*)*it))->setShowVoltageBars( KTLConfig::showVoltageBars() );
	}

	ComponentList::iterator componentsEnd = m_componentList.end();
	for(ComponentList::iterator it = m_componentList.begin(); it != componentsEnd; ++it)
		(*it)->slotUpdateConfiguration();
}

void CircuitDocument::update()
{
	ICNDocument::update();

	if(KTLConfig::showVoltageBars()) {
		NodeList::iterator end = m_nodeList.end();
		for(NodeList::iterator it = m_nodeList.begin(); it != end; ++it ) {
			(static_cast<ECNode*>((Node*)*it))->setNodeChanged();
		}
	}
}

void CircuitDocument::fillContextMenu( const QPoint &pos )
{
	ICNDocument::fillContextMenu(pos);

	CircuitView *activeCircuitView = dynamic_cast<CircuitView*>(activeView());

	if(m_selectList->count() < 1 || !activeCircuitView ) return;

	Component *item = dynamic_cast<Component*>( selectList()->activeItem() );

	// NOTE: I negated this whole condition because I couldn't make out quite what the
	//logic was --electronerd
	if(!(!item && m_selectList->count() > 0 || !m_selectList->itemsAreSameType() )) {
		KAction *orientation_actions[] = {
			activeCircuitView->action("edit_orientation_0"),
			activeCircuitView->action("edit_orientation_90"),
			activeCircuitView->action("edit_orientation_180"),
			activeCircuitView->action("edit_orientation_270") };
	
		if(!item || !item->canRotate() ) return;
	
		for(unsigned i = 0; i < 4; ++ i) {
			m_pOrientationAction->remove( orientation_actions[i] );
			m_pOrientationAction->insert( orientation_actions[i] );
		}

		QPtrList<KAction> orientation_actionlist;
	// 	orientation_actionlist.prepend( new KActionSeparator() );
		orientation_actionlist.append( m_pOrientationAction );
		p_ktechlab->plugActionList( "orientation_actionlist", orientation_actionlist );
	}
	
	if(m_selectList->count() > 1 && countExtCon(m_selectList->items()) > 0) {
		QPtrList<KAction> component_actionlist;
	// 	component_actionlist.append( new KActionSeparator() );
		component_actionlist.append( activeCircuitView->action("circuit_create_subcircuit") );
		p_ktechlab->plugActionList( "component_actionlist", component_actionlist );
	}
}

void CircuitDocument::deleteCircuits()
{
	const CircuitList::iterator end = m_circuitList.end();
	for(CircuitList::iterator it = m_circuitList.begin(); it != end; ++it ) {
		Simulator::self()->detachCircuit(*it);
		delete *it;
	}

	m_circuitList.clear();
	m_pinList.clear();
	m_wireList.clear();
}

void CircuitDocument::requestAssignCircuits() {
// 	kdDebug() << k_funcinfo << endl;
	deleteCircuits();
	m_updateCircuitsTmr->stop();
	m_updateCircuitsTmr->start( 0, true );
}

void CircuitDocument::connectorAdded( Connector * connector )
{
	if (connector) {
		connect( connector, SIGNAL(numWiresChanged(unsigned )), this, SLOT(requestAssignCircuits()) );
		connect( connector, SIGNAL(removed(Connector*)), this, SLOT(requestAssignCircuits()) );
	}
}

void CircuitDocument::itemAdded( Item * item)
{
	ICNDocument::itemAdded( item );
	componentAdded( item );
}

void CircuitDocument::componentAdded( Item * item )
{
	Component *component = dynamic_cast<Component*>(item);

	if(!component) return;

	requestAssignCircuits();

	connect( component, SIGNAL(elementCreated(Element*)), this, SLOT(requestAssignCircuits()) );
	connect( component, SIGNAL(elementDestroyed(Element*)), this, SLOT(requestAssignCircuits()) );
	connect( component, SIGNAL(removed(Item*)), this, SLOT(componentRemoved(Item*)) );
	
	// We don't attach the component to the Simulator just yet, as the
	// Component's vtable is not yet fully constructed, and so Simulator can't
	// tell whether or not it is a logic component
	if ( !m_toSimulateList.contains(component) )
		m_toSimulateList << component;
}

void CircuitDocument::componentRemoved( Item * item )
{
	Component *component = dynamic_cast<Component*>(item);

	if (!component) return;

	m_componentList.remove(component);

	requestAssignCircuits();
	Simulator::self()->detachComponent(component);
}

void CircuitDocument::calculateConnectorCurrents()
{
	const CircuitList::iterator circuitEnd = m_circuitList.end();
	for(CircuitList::iterator it = m_circuitList.begin(); it != circuitEnd; ++it )
		(*it)->updateCurrents();

	PinList groundPins;

	// Tell the Pins to reset their calculated currents to zero

	m_pinList.remove((Pin*)0);
	const PinList::iterator pinEnd = m_pinList.end();
	for(PinList::iterator it = m_pinList.begin(); it != pinEnd; ++it )  {
		if(Pin *n = dynamic_cast<Pin*>((Pin*)*it) ) {
			n->resetCurrent();
			n->setSwitchCurrentsUnknown();
			
			if ( !n->parentECNode()->isChildNode() ) {
				n->setCurrentKnown( true );
				// (and it has a current of 0 amps)
			} else if ( n->groundType() == Pin::gt_always )	{
				groundPins << n;
				n->setCurrentKnown( false );
			} else {
				// Child node that is non ground
				n->setCurrentKnown( n->parentECNode()->numPins() < 2 );
			}
		}
	}

	// Tell the components to update their ECNode's currents' from the elements
	const ComponentList::iterator componentEnd = m_componentList.end();
	for ( ComponentList::iterator it = m_componentList.begin(); it != componentEnd; ++it ) (*it)->setNodalCurrents();

	// And now for the wires and switches...
	m_wireList.remove((Wire*)0);
	const WireList::iterator clEnd = m_wireList.end();
	for ( WireList::iterator it = m_wireList.begin(); it != clEnd; ++it )
		(*it)->setCurrentKnown(false);

	SwitchList switches = m_switchList;
	WireList wires = m_wireList;
	bool found = true;
	while((!wires.isEmpty() || !switches.isEmpty() || !groundPins.isEmpty()) && found ) {
		found = false;
		
		WireList::iterator wiresEnd = wires.end();
		for ( WireList::iterator it = wires.begin(); it != wiresEnd; )
		{
			if ( (*it)->calculateCurrent() )  {
				found = true;
				WireList::iterator oldIt = it;
				++it;
				wires.remove(oldIt);
			} else ++it;
		}

		SwitchList::iterator switchesEnd = switches.end();
		for(SwitchList::iterator it = switches.begin(); it != switchesEnd; ) {
			if ( (*it)->calculateCurrent() ) {
				found = true;
				SwitchList::iterator oldIt = it;
				++it;
				switches.remove(oldIt);
			} else ++it;
		}

		PinList::iterator groundPinsEnd = groundPins.end();
		for(PinList::iterator it = groundPins.begin(); it != groundPinsEnd; ) {
			if ( (*it)->calculateCurrentFromWires() )
			{
				found = true;
				PinList::iterator oldIt = it;
				++it;
				groundPins.remove(oldIt);
			} else ++it;
		}
	}
}

void CircuitDocument::assignCircuits()
{
	// Now we can finally add the unadded components to the Simulator
	const ComponentList::iterator toSimulateEnd = m_toSimulateList.end();
	for ( ComponentList::iterator it = m_toSimulateList.begin(); it != toSimulateEnd; ++it )
		Simulator::self()->attachComponent(*it);
	m_toSimulateList.clear();
	
	// Stage 0: Build up pin and wire lists
	m_pinList.clear();
	const NodeList::const_iterator nodeListEnd = m_nodeList.end();
	for ( NodeList::const_iterator it = m_nodeList.begin(); it != nodeListEnd; ++it ) {
		if ( ECNode * ecnode = dynamic_cast<ECNode*>((Node*)*it) ) {
			for ( unsigned i = 0; i < ecnode->numPins(); i++ )
				m_pinList << ecnode->pin(i);
		}
	}

	m_wireList.clear();
	const ConnectorList::const_iterator connectorListEnd = m_connectorList.end();
	for ( ConnectorList::const_iterator it = m_connectorList.begin(); it != connectorListEnd; ++it ) {
		for ( unsigned i = 0; i < (*it)->numWires(); i++ )
			m_wireList << (*it)->wire(i);
	}

	typedef QValueList<PinList> PinListList;

	// Stage 1: Partition the circuit up into dependent areas (bar splitting
	// at ground pins)
	PinList unassignedPins = m_pinList;
	PinListList pinListList;

	while(!unassignedPins.isEmpty() ) {
		PinList pinList;
		getPartition( *unassignedPins.begin(), & pinList, & unassignedPins );
		pinListList.append(pinList);
	}
// 	kdDebug () << "pinListList.size()="<<pinListList.size()<<endl;
	
	// Stage 2: Split up each partition into circuits by ground pins
	const PinListList::iterator nllEnd = pinListList.end();

	for ( PinListList::iterator it = pinListList.begin(); it != nllEnd; ++it )
		splitIntoCircuits(&*it);
	
	// Stage 3: Initialize the circuits
	m_circuitList.remove(0);
	CircuitList::iterator circuitListEnd = m_circuitList.end();

	for(CircuitList::iterator it = m_circuitList.begin(); it != circuitListEnd; ++it) (*it)->init();

	m_switchList.clear();
	m_componentList.clear();
	const ItemList::const_iterator cilEnd = m_itemList.end();

	for(ItemList::const_iterator it = m_itemList.begin(); it != cilEnd; ++it ) {
		Component * component = dynamic_cast<Component*>((Item*)(*it));
		if ( !component ) continue;

		m_componentList << component;
		component->initElements(0);
		m_switchList += component->switchList();
	}

	circuitListEnd = m_circuitList.end();
	for ( CircuitList::iterator it = m_circuitList.begin(); it != circuitListEnd; ++it )
		(*it)->createMatrixMap();
	
	for(ItemList::const_iterator it = m_itemList.begin(); it != cilEnd; ++it ) {
		Component * component = dynamic_cast<Component*>((Item*)(*it));
		if (component)
			component->initElements(1);
	}
	
	circuitListEnd = m_circuitList.end();
	for ( CircuitList::iterator it = m_circuitList.begin(); it != circuitListEnd; ++it ) {
		(*it)->initCache();
		Simulator::self()->attachCircuit(*it);
	}
}

void CircuitDocument::getPartition( Pin *pin, PinList *pinList, PinList *unassignedPins, bool onlyGroundDependent ) {
	if(!pin) return;

	unassignedPins->remove(pin);
	
	if ( pinList->contains(pin) ) return;

	pinList->append(pin);

	const PinList localConnectedPins = pin->localConnectedPins();
	const PinList::const_iterator end = localConnectedPins.end();

	for ( PinList::const_iterator it = localConnectedPins.begin(); it != end; ++it )
		getPartition( *it, pinList, unassignedPins, onlyGroundDependent );
	
	const PinList groundDependentPins = pin->groundDependentPins();
	const PinList::const_iterator dEnd = groundDependentPins.end();

	for ( PinList::const_iterator it = groundDependentPins.begin(); it != dEnd; ++it )
		getPartition( *it, pinList, unassignedPins, onlyGroundDependent );

	if (!onlyGroundDependent) {
		PinList circuitDependentPins = pin->circuitDependentPins();
		const PinList::const_iterator dEnd = circuitDependentPins.end();
		for ( PinList::const_iterator it = circuitDependentPins.begin(); it != dEnd; ++it )
			getPartition( *it, pinList, unassignedPins, onlyGroundDependent );
	}
}

void CircuitDocument::splitIntoCircuits( PinList *pinList )
{
	// First: identify ground
	PinList unassignedPins = *pinList;

	typedef QValueList<PinList> PinListList;

	PinListList pinListList;
	while ( !unassignedPins.isEmpty() ) {
		PinList tempPinList;
		getPartition( *unassignedPins.begin(), & tempPinList, & unassignedPins, true );
		pinListList.append(tempPinList);
	}

	const PinListList::iterator nllEnd = pinListList.end();
	for(PinListList::iterator it = pinListList.begin(); it != nllEnd; ++it )
		Circuit::identifyGround(*it);

	bool allGround = false;
	while(!pinList->isEmpty() && !allGround ) {
		PinList::iterator end = pinList->end();
		PinList::iterator it = pinList->begin();

		while(it != end && (*it)->eqId() == -1 )
			++it;

		if ( it == end ) allGround = true;
		else {
			Circuitoid *circuitoid = new Circuitoid;
			recursivePinAdd( *it, circuitoid, pinList );

			if ( !tryAsLogicCircuit(circuitoid) )
				m_circuitList += createCircuit(circuitoid);

			delete circuitoid;
		}
	}

	// Remaining pins are ground; tell them about it
	// TODO This is a bit hacky....
	const PinList::iterator end = pinList->end();
	for(PinList::iterator it = pinList->begin(); it != end; ++it ) {
		(*it)->setVoltage(0.0);

		ElementList elements = (*it)->elements();
		const ElementList::iterator eEnd = elements.end();
		for(ElementList::iterator it = elements.begin(); it != eEnd; ++it )
		{
			if(LogicIn * logicIn = dynamic_cast<LogicIn*>(*it) ) {
				logicIn->setLastState(false);
				logicIn->callCallback();
			}
		}
	}
}

void CircuitDocument::recursivePinAdd( Pin *pin, Circuitoid *circuitoid, PinList *unassignedPins )
{
	if (!pin) return;

	if ( pin->eqId() != -1 )
		unassignedPins->remove(pin);

	if ( circuitoid->contains(pin) ) return;

	circuitoid->addPin(pin);

	if ( pin->eqId() == -1 ) return;

	const PinList localConnectedPins = pin->localConnectedPins();
	const PinList::const_iterator end = localConnectedPins.end();

	for ( PinList::const_iterator it = localConnectedPins.begin(); it != end; ++it )
		recursivePinAdd( *it, circuitoid, unassignedPins );

	const WireList inputList = pin->inputWireList();
	WireList::const_iterator conEnd = inputList.end();

	for ( WireList::const_iterator it = inputList.begin(); it != conEnd; ++it )
		circuitoid->addWire(*it);

	const WireList outputList = pin->outputWireList();
	conEnd = outputList.end();

	for ( WireList::const_iterator it = outputList.begin(); it != conEnd; ++it )
		circuitoid->addWire(*it);
	
	const PinList groundDependentPins = pin->groundDependentPins();
	const PinList::const_iterator gdEnd = groundDependentPins.end();

	for ( PinList::const_iterator it = groundDependentPins.begin(); it != gdEnd; ++it )
		recursivePinAdd( *it, circuitoid, unassignedPins );

	const PinList circuitDependentPins = pin->circuitDependentPins();
	const PinList::const_iterator cdEnd = circuitDependentPins.end();

	for ( PinList::const_iterator it = circuitDependentPins.begin(); it != cdEnd; ++it )
		recursivePinAdd( *it, circuitoid, unassignedPins );
	
	const ElementList elements = pin->elements();
	const ElementList::const_iterator eEnd = elements.end();

	for ( ElementList::const_iterator it = elements.begin(); it != eEnd; ++it )
		circuitoid->addElement(*it);
}

bool CircuitDocument::tryAsLogicCircuit( Circuitoid *circuitoid )
{
	if (!circuitoid) return false;
	
	if ( circuitoid->elementList.size() == 0 )
	{
		// This doesn't quite belong here...but whatever. Initialize all
		// pins to voltage zero as they won't get set to zero otherwise
		const PinList::const_iterator pinListEnd = circuitoid->pinList.constEnd();
		for ( PinList::const_iterator it = circuitoid->pinList.constBegin(); it != pinListEnd; ++it )
			(*it)->setVoltage(0.0);
		
		// A logic circuit only requires there to be no non-logic components,
		// and at most one LogicOut - so this qualifies
		return true;
	}

	LogicInList logicInList;
	LogicOut *out = 0;

	uint logicInCount = 0;
	uint logicOutCount = 0;
	ElementList::const_iterator end = circuitoid->elementList.end();
	for ( ElementList::const_iterator it = circuitoid->elementList.begin(); it != end; ++it ) {
		if ( (*it)->type() == Element::Element_LogicOut ) {
			logicOutCount++;
			out = static_cast<LogicOut*>(*it);
		} else if ( (*it)->type() == Element::Element_LogicIn ) {
			logicInCount++;
			logicInList += static_cast<LogicIn*>(*it);
		} else return false;
	}

	if ( logicOutCount > 1 ) return false;
	else if ( logicOutCount == 1 )
		Simulator::self()->createLogicChain( out, logicInList, circuitoid->pinList );
	else {
		// We have ourselves stranded LogicIns...so lets set them all to low

		const PinList::const_iterator pinListEnd = circuitoid->pinList.constEnd();
		for ( PinList::const_iterator it = circuitoid->pinList.constBegin(); it != pinListEnd; ++it )
			(*it)->setVoltage(0.0);

		for(ElementList::const_iterator it = circuitoid->elementList.begin(); it != end; ++it) {
			LogicIn * logicIn = static_cast<LogicIn*>(*it);
			logicIn->setNextLogic(0);
			logicIn->setElementSet(0);
			if ( logicIn->isHigh() ) {
				logicIn->setLastState(false);
				logicIn->callCallback();
			}
		}
	}

	return true;
}

Circuit *CircuitDocument::createCircuit( Circuitoid *circuitoid )
{
	if (!circuitoid) return 0;

	Circuit *circuit = new Circuit();

	const PinList::const_iterator nEnd = circuitoid->pinList.end();
	for ( PinList::const_iterator it = circuitoid->pinList.begin(); it != nEnd; ++it )
		circuit->addPin(*it);

	const ElementList::const_iterator eEnd = circuitoid->elementList.end();
	for ( ElementList::const_iterator it = circuitoid->elementList.begin(); it != eEnd; ++it )
		circuit->addElement(*it);

	return circuit;
}

void CircuitDocument::createSubcircuit()
{
	ItemList itemList = m_selectList->items();
	const ItemList::iterator itemListEnd = itemList.end();
	for ( ItemList::iterator it = itemList.begin(); it != itemListEnd; ++it ) {
		if( !dynamic_cast<Component*>((Item*)*it) ) *it = 0;
	}

	itemList.remove((Item*)0);
	
	if ( itemList.isEmpty() ) {
		KMessageBox::sorry( activeView(), i18n("No components were found in the selection.") );
		return;
	}

	// Number of external connections
	const int extConCount = countExtCon(itemList);
	if ( extConCount == 0 ) {
		KMessageBox::sorry( activeView(), i18n("No External Connection components were found in the selection.") );
		return;
	}
	
	bool ok;
	const QString name = KInputDialog::getText( "Subcircuit", "Name", QString::null, &ok, activeView() );

	if (!ok) return;

	SubcircuitData subcircuit;
	subcircuit.addItems(itemList);
	subcircuit.addNodes( getCommonNodes(itemList) );
	subcircuit.addConnectors( getCommonConnectors(itemList) );

	Subcircuits::addSubcircuit( name, subcircuit.toXML() );
}

int CircuitDocument::countExtCon( const ItemList &itemList ) const
{
	int count = 0;
	const ItemList::const_iterator end = itemList.end();
	for(ItemList::const_iterator it = itemList.begin(); it != end; ++it ) {
		Item *item = *it;
		if(item && item->type() == "ec/external_connection" )
			count++;
	}
	return count;
}

bool CircuitDocument::isValidItem( const QString &itemId )
{
	return itemId.startsWith("ec/") || itemId.startsWith("dp/") || itemId.startsWith("sc/");
}

bool CircuitDocument::isValidItem( Item *item )
{
	return (dynamic_cast<Component*>(item) || dynamic_cast<DrawPart*>(item));
}

void CircuitDocument::displayEquations()
{
	kdDebug() << "######################################################" << endl;
	const CircuitList::iterator end = m_circuitList.end();
	int i = 1;
	for(CircuitList::iterator it = m_circuitList.begin(); it != end; ++it ) {
		kdDebug() << "Equation set "<<i<<":\n";
		(*it)->displayEquations();
		i++;
	}
	kdDebug() << "######################################################" << endl;
}

#include "circuitdocument.moc"

