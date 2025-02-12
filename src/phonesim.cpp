/****************************************************************************
**
** This file is part of the Qt Extended Opensource Package.
**
** Copyright (C) 2009 Trolltech ASA.
**
** Contact: Qt Extended Information (info@qtextended.org)
**
** This file may be used under the terms of the GNU General Public License
** version 2.0 as published by the Free Software Foundation and appearing
** in the file LICENSE.GPL included in the packaging of this file.
**
** Please review the following information to ensure GNU General Public
** Licensing requirements will be met:
**     http://www.fsf.org/licensing/licenses/info/GPLv2.html.
**
**
****************************************************************************/

#include "phonesim.h"

#include "hardwaremanipulator.h"
#include "simfilesystem.h"
#include "simapplication.h"
#include "callmanager.h"
#include "simauth.h"
#include "aidapplication.h"
#include <qatutils.h>

#include <qstring.h>
#include <qbytearray.h>
#include <qregexp.h>
#include <qdebug.h>

#define PHONEBOOK_NLENGTH 32
#define PHONEBOOK_TLENGTH 16
#define PHONEBOOK_GLENGTH 255
#define PHONEBOOK_SLENGTH 16
#define PHONEBOOK_ELENGTH 255
#define PHONEBOOK_SIPLENGTH 255
#define PHONEBOOK_TELLENGTH 255

#define INVALID_VALUE_HIDDEN -1

SimXmlNode::SimXmlNode( const QString& _tag )
{
    parent = 0;
    next = 0;
    children = 0;
    attributes = 0;
    tag = _tag;
}


SimXmlNode::~SimXmlNode()
{
    SimXmlNode *temp1, *temp2;
    temp1 = children;
    while ( temp1 ) {
        temp2 = temp1->next;
        delete temp1;
        temp1 = temp2;
    }
    temp1 = attributes;
    while ( temp1 ) {
        temp2 = temp1->next;
        delete temp1;
        temp1 = temp2;
    }
}

void SimXmlNode::addChild( SimXmlNode *child )
{
    SimXmlNode *current = children;
    SimXmlNode *prev = 0;
    while ( current ) {
        prev = current;
        current = current->next;
    }
    if ( prev ) {
        prev->next = child;
    } else {
        children = child;
    }
    child->next = 0;
    child->parent = this;
}


void SimXmlNode::addAttribute( SimXmlNode *child )
{
    SimXmlNode *current = attributes;
    SimXmlNode *prev = 0;
    while ( current ) {
        prev = current;
        current = current->next;
    }
    if ( prev ) {
        prev->next = child;
    } else {
        attributes = child;
    }
    child->next = 0;
    child->parent = this;
}


QString SimXmlNode::getAttribute( const QString& name )
{
    SimXmlNode *current = attributes;
    while ( current ) {
        if ( current->tag == name )
            return current->contents;
        current = current->next;
    }
    return QString();
}


SimXmlHandler::SimXmlHandler()
{
    tree = new SimXmlNode( QString() );
    current = tree;
}


SimXmlHandler::~SimXmlHandler()
{
    delete tree;
}


bool SimXmlHandler::startElement( const QString& name, const QXmlStreamAttributes& atts )
{
    SimXmlNode *node = new SimXmlNode( name );
    SimXmlNode *attr;
    int index;
    current->addChild( node );
    for ( index = 0; index < atts.size(); ++index ) {
        attr = new SimXmlNode( atts[index].name().toString() );
        attr->contents = atts[index].value().toString();
        node->addAttribute( attr );
    }
    current = node;
    return true;
}


bool SimXmlHandler::endElement()
{
    current = current->parent;
    return true;
}


bool SimXmlHandler::characters( const QString& ch )
{
    current->contents += ch;
    return true;
}


SimXmlNode *SimXmlHandler::documentElement() const
{
    if ( tree->children && tree->children->tag == "simulator" ) {
        return tree->children;
    } else {
        return tree;
    }
}


SimState::SimState( SimRules *rules, SimXmlNode& e )
{
    _rules = rules;
    if ( e.tag == "state" ) {
        _name = e.getAttribute( "name" );
    }
    SimXmlNode *n = e.children;
    while ( n != 0 ) {
        if ( n->tag == "chat" ) {

            // Load a chat response definition.
            items.append( new SimChat( this, *n ) );

        } else if ( n->tag == "unsolicited" ) {

            // Load an unsolicited response definition.
            items.append( new SimUnsolicited( this, *n ) );

        }
        n = n->next;
    }
}


void SimState::enter()
{
    QList<SimItem *>::Iterator iter;
    for ( iter = items.begin(); iter != items.end(); ++iter ) {
        (*iter)->enter();
    }
}


void SimState::leave()
{
    QList<SimItem *>::Iterator iter;
    for ( iter = items.begin(); iter != items.end(); ++iter ) {
        (*iter)->leave();
    }
}


bool SimState::command( const QString& cmd )
{
    // Search for a "SimChat" item that understands the command.
    QList<SimItem *>::Iterator iter;
    for ( iter = items.begin(); iter != items.end(); ++iter ) {
        if ( (*iter)->command( cmd ) ) {
            return true;
        }
    }

    // Pass unhandled commands to the default state to be processed.
    SimState *defaultState = rules()->defaultState();
    if ( defaultState != this ) {
        return defaultState->command( cmd );
    } else {
        return false;
    }
}


SimChat::SimChat( SimState *state, SimXmlNode& e )
    : SimItem( state )
{
    SimXmlNode *n = e.children;
    responseDelay = 0;
    wildcard = false;
    eol = true;

    listSMS = false;
    deleteSMS = false;
    readSMS = false;

    while ( n != 0 ) {
        if ( n->tag == "command" ) {
            _command = n->contents;
            int w=_command.indexOf(QChar('*'));
            while(w <= 2 && w >= 0)
                w=_command.indexOf(QChar('*'), w+1);
            if ( w > 2 ) {
                wildcard = true;
            } else {
                wildcard = false;
            }
            QString wc = n->getAttribute( "wildcard" );
            if ( wc == "true" )
                wildcard = true;    // Force the use of wildcarding.
        } else if ( n->tag == "response" ) {
            QString delay = n->getAttribute( "delay" );
            response = n->contents;
            if ( delay != QString() )
                responseDelay = delay.toInt();
            else
                responseDelay = 0;
            QString eolstr = n->getAttribute( "eol" );
            eol = (eolstr != "false");
        } else if ( n->tag == "switch" ) {
            switchTo = n->getAttribute( "name" );
        } else if ( n->tag == "set" ) {
            QString name = n->getAttribute( "name" );
	    variables += name;
	    values += n->getAttribute( "value" );
            int delay = n->getAttribute( "delay" ).toInt();
            if (delay)
                delays[name] = delay;
        } else if ( n->tag == "newcall" ) {
            newCallVar = n->getAttribute( "name" );
        } else if ( n->tag == "forgetcall" ) {
            forgetCallId = n->getAttribute( "id" );
        } else if ( n->tag == "listSMS" ) {
            listSMS = true;
        } else if ( n->tag =="deleteSMS" ) {
            deleteSMS = true;
        } else if ( n->tag == "readSMS" ) {
            readSMS = true;
        }

        n = n->next;
    }
}

QString PS_toHex( const QByteArray& binary )
{
    QString str = "";
    static char const hexchars[] = "0123456789ABCDEF";

    for ( int i = 0; i < binary.size(); i++ ) {
        str += (QChar)(hexchars[ (binary[i] >> 4) & 0x0F ]);
        str += (QChar)(hexchars[ binary[i] & 0x0F ]);
    }

    return str;
}

bool SimChat::command( const QString& cmd )
{
    QString wild;
    // command may contain vars, expand them.
    QString _ecommand = state()->rules()->expand(_command);

    if ( wildcard ) {
        int s=QRegExp(_ecommand,Qt::CaseSensitive,QRegExp::Wildcard).indexIn(cmd,0);
        if (s==0) {
            int w=_ecommand.indexOf(QChar('*'));
            while(w <= 2 && w >= 0)
                w=_ecommand.indexOf(QChar('*'),w+1);
            wild = cmd.mid(w,cmd.length()-_ecommand.length()+1);
        } else
            return false;
    } else if ( !wildcard && cmd == _ecommand ) {

        // Matched the entire command.
        wild = "";

    } else {
        return false;
    }

    // Send the response.
    if (!readSMS && !deleteSMS && !listSMS)
        state()->rules()->respond( response, responseDelay, eol );

    // Set the variables.
    for ( int varNum = 0; varNum < variables.size(); ++varNum ) {
    	QString variable = variables[varNum];
	QString value = values[varNum];
        int delay = delays.value(variable, 0);
        QString val;

        if ( value == "*" )
            val = wild;
        else {
            int index = value.indexOf( "${*}" );
            if ( index != -1 ) {
                if ( wild.length() > 0 && wild[wild.length() - 1] == 0x1A ) {
                    // Strip the terminating ^Z from SMS PDU's.
                    wild = wild.left( wild.length() - 1 );
                }

                val = value.left( index ) + wild + value.mid( index + 4 );
            } else
                val = value;
        }

        if (delay) {
            QVariantTimer *timer = new QVariantTimer(this->state()->rules());

            timer->param = QVariant::fromValue(QPairKV(variable, val));

            timer->setSingleShot( true );

            connect(timer, SIGNAL(timeout()), this->state()->rules(),
                    SLOT(delaySetVariable()));
            timer->start( delay );
        } else
            state()->rules()->setVariable( variable, val );
    }

    // Switch to the new state.
    if ( switchTo != QString() ) {
        state()->rules()->switchTo( switchTo );
    }

    // Allocate a new call identifier or forget this call identifier.
    if ( newCallVar.length() > 0 ) {
        state()->rules()->setVariable
            ( newCallVar, QString::number( state()->rules()->newCall() ) );
    }
    if ( forgetCallId.length() > 0 ) {
        if ( forgetCallId == "*" )
            if ( wild.length() == 0 )
                state()->rules()->forgetAllCalls();
            else
                state()->rules()->forgetCall( wild.toInt() );
        else
            state()->rules()->forgetCall
                ( state()->rules()->expand( forgetCallId ).toInt() );
    }
    if ( listSMS && state()->rules()->getMachine() ) {
        QString listSMSResponse;
        QSMSMessageList &SMSList = state()->rules()->getMachine()->getSMSList();
        QString status;

        if ( state()->rules()->variable("MSGMEM") == "SM" ) {
            for ( int i=0; i<SMSList.count(); i++ ) {
                if ( SMSList.getDeletedFlag(i) == true )
                    continue;

                status = QString::number(SMSList.getStatus(i));
                listSMSResponse.append("+CMGL: " + QString::number(i+1) + "," + status + ",," +
                                       QString::number(SMSList.getLength(i)) + "\\n" +
                                       PS_toHex( SMSList.readSMS(i) ) + "\\n");
            }
        }

	if (listSMSResponse.isEmpty())
            listSMSResponse.append("+CMS ERROR: 321");
	else
            listSMSResponse.append("\\nOK");

        state()->rules()->respond(listSMSResponse , responseDelay, eol );
    }

    if ( deleteSMS && state()->rules()->getMachine() ) {
        QString deleteSMSResponse;
        QSMSMessageList &SMSList = state()->rules()->getMachine()->getSMSList();
        int index = wild.toInt();

        if ( index > SMSList.count() || index <= 0 || (SMSList.getDeletedFlag(index-1) == true) ) {
            deleteSMSResponse.append("ERROR");
        } else {
            SMSList.deleteSMS(index-1);
            deleteSMSResponse.append("OK");
        }

        state()->rules()->respond(deleteSMSResponse , responseDelay, eol );
    }

    if ( readSMS && state()->rules()->getMachine() ) {
        QString readSMSResponse;
        QSMSMessageList &SMSList = state()->rules()->getMachine()->getSMSList();
        int index = wild.toInt();

        if ( index > SMSList.count() || index <= 0 || (SMSList.getDeletedFlag(index-1) == true) ) {
            readSMSResponse.append("ERROR");
        } else {
            QString status = QString::number(SMSList.getStatus(index-1));
            readSMSResponse.append("+CMGR: " + status + ",," +
                                       QString::number(SMSList.getLength(index-1)) + "\\n" +
                                       PS_toHex( SMSList.readSMS(index-1) ) + "\\n");
        }

	readSMSResponse += "\\nOK";
        state()->rules()->respond(readSMSResponse , responseDelay, eol );
    }
    return true;
}


SimUnsolicited::SimUnsolicited( SimState *state, SimXmlNode& e )
    : SimItem( state ), done(false)
{
    QString delay = e.getAttribute( "delay" );
    response = e.contents;
    if ( delay != QString() )
        responseDelay = delay.toInt();
    else
        responseDelay = 0;
    switchTo = e.getAttribute( "switch" );
    doOnce = e.getAttribute( "once" ) == "true";

    timer = new QTimer( this );
    timer->setSingleShot( true );
    connect( timer, SIGNAL(timeout()), this, SLOT(timeout()) );
}


void SimUnsolicited::enter()
{
    if (!doOnce || !done)
        timer->start( responseDelay );
}


void SimUnsolicited::leave()
{
    timer->stop();
}


void SimUnsolicited::timeout()
{
    if (state() && state()->rules()) {
        state()->rules()->unsolicited( response );

        if ( switchTo != QString() ) {
            state()->rules()->switchTo( switchTo );
        }
    }

    done = true;
}

static bool readXmlFile( SimXmlHandler *handler, const QString& filename )
{
    QFile f( filename );
    if ( !f.open( QIODevice::ReadOnly ) )
        return false;
    QXmlStreamReader reader( &f );
    while ( !reader.atEnd() ) {
        reader.readNext();
        if ( reader.hasError() )
            break;
        if ( reader.isStartElement() ) {
            handler->startElement( reader.name().toString(), reader.attributes() );
        } else if ( reader.isEndElement() ) {
            handler->endElement();
        } else if ( reader.isCharacters() ) {
            handler->characters( reader.text().toString() );
        }
    }
    f.close();
    return !reader.hasError();
}

SimRules::SimRules( int fd, QObject *p,  const QString& filename, HardwareManipulatorFactory *hmf )
    : QTcpSocket(p)
{
    setSocketDescriptor(fd);
    machine = 0;
    toolkitApp = 0;

    if (hmf)
        machine = hmf->create(this, 0);

    if (machine) {
        connect(machine, SIGNAL(unsolicitedCommand(QString)),
                this, SLOT(unsolicited(QString)));
        connect(machine, SIGNAL(command(QString)),
                this, SLOT(command(QString)));
        connect(machine, SIGNAL(variableChanged(QString,QString)),
                this, SLOT(setVariable(QString,QString)));
        connect(machine, SIGNAL(switchTo(QString)),
                this, SLOT(switchTo(QString)));
    }

    _callManager = new CallManager(this);
    connect( _callManager, SIGNAL(send(QString)),
             this, SLOT(respond(QString)) );
    connect( _callManager, SIGNAL(unsolicited(QString)),
             this, SLOT(unsolicited(QString)) );
    connect( _callManager, SIGNAL(dialCheck(QString,bool&)),
             this, SLOT(dialCheck(QString,bool&)) );

    if ( machine ) {
        connect( machine, SIGNAL(startIncomingCall(QString,QString,QString)),
                 _callManager, SLOT(startIncomingCall(QString,QString,QString)) );
        connect ( _callManager, SIGNAL( callStatesChanged( QList<CallInfo> * ) ),
                  machine, SLOT( callManagement( QList<CallInfo> * ) ) );
        connect ( machine, SIGNAL( stateChangedToAlerting() ), _callManager,
                SLOT( dialingToAlerting() ) );
        connect ( machine, SIGNAL( stateChangedToConnected() ), _callManager,
                SLOT( dialingToConnected() ) );
        connect ( machine, SIGNAL( stateChangedToHangup( int ) ), _callManager,
                SLOT( hangupRemote( int ) ) );
    }

    connect(this,SIGNAL(readyRead()),
        this,SLOT(tryReadCommand()));
    connect(this,SIGNAL(disconnected()),
        this,SLOT(destruct()));
    // Initialize the local state.
    currentState = 0;
    defState = 0;
    usedCallIds = 0;
    fileSystem = 0;
    useGsm0710 = false;
    currentChannel = 1;
    incomingUsed = 0;
    lineUsed = 0;
    defaultToolkitApp = toolkitApp = new DemoSimApplication( this, this );
    conformanceApp = new ConformanceSimApplication( this, this );
    connect( _callManager, SIGNAL(controlEvent(QSimControlEvent)),
             toolkitApp, SLOT(controlEvent(QSimControlEvent)) );

    simApps.append( toolkitApp );
    simApps.append( conformanceApp );

    if ( machine )
        machine->handleNewApp();

    // Load the simulator rules into memory as a DOM-like tree.
    SimXmlHandler *handler = new SimXmlHandler();
    if ( !readXmlFile( handler, filename ) ) {
        qWarning() << filename << ": could not parse simulator rule file";
        delete handler;
        return;
    }

    // Load the default state and set it as current.
    defState = new SimState( this, *(handler->documentElement()) );
    states.append( defState );

    initPhoneBooks();

    // Load the other states, and the start state's name (if specified).
    SimXmlNode *n = handler->documentElement()->children;
    QString start = QString();
    while ( n != 0 ) {
        if ( n->tag == "state" ) {

            // Load a new state definition.
            SimState *state = new SimState( this, *n );
            states.append( state );

        } else if ( n->tag == "start" ) {

            // Set a new start state.
            start = n->getAttribute( "name" );

        } else if ( n->tag == "set" ) {

            // Set the initial value of a variable.
            QString name = n->getAttribute( "name" );
            QString value = n->getAttribute( "value" );
            if ( name != QString() && value != QString() ) {
                setVariable(name, value);
            }

        } else if ( n->tag == "filesystem" ) {

            // Load the SIM filesystem.
            fileSystem = new SimFileSystem( this, *n );

        } else if ( n->tag == "phonebook" ) {

            // Load a phonebook definition.
            loadPhoneBook( *n );

        } else if ( n->tag == "simauth" ) {

            _simAuth = new SimAuth( this, *n );

        } else if ( n->tag == "application" ) {
            AidApplication *app = new AidApplication( this, *n );
            _applications.append(app);
        }
        n = n->next;
    }

    if ( _applications.length() > 0 )
        _app_wrapper = new AidAppWrapper( this, _applications, _simAuth );

    // Clean up the XML reader objects.
    delete handler;

    // Set the start state appropriately.
    currentState = state( start );
    if ( !currentState )
        currentState = defState;
    currentState->enter();
}


#define MAX_GSM0710_FRAME_SIZE      31


static const unsigned char crcTable[256] = {
    0x00, 0x91, 0xE3, 0x72, 0x07, 0x96, 0xE4, 0x75,
    0x0E, 0x9F, 0xED, 0x7C, 0x09, 0x98, 0xEA, 0x7B,
    0x1C, 0x8D, 0xFF, 0x6E, 0x1B, 0x8A, 0xF8, 0x69,
    0x12, 0x83, 0xF1, 0x60, 0x15, 0x84, 0xF6, 0x67,
    0x38, 0xA9, 0xDB, 0x4A, 0x3F, 0xAE, 0xDC, 0x4D,
    0x36, 0xA7, 0xD5, 0x44, 0x31, 0xA0, 0xD2, 0x43,
    0x24, 0xB5, 0xC7, 0x56, 0x23, 0xB2, 0xC0, 0x51,
    0x2A, 0xBB, 0xC9, 0x58, 0x2D, 0xBC, 0xCE, 0x5F,
    0x70, 0xE1, 0x93, 0x02, 0x77, 0xE6, 0x94, 0x05,
    0x7E, 0xEF, 0x9D, 0x0C, 0x79, 0xE8, 0x9A, 0x0B,
    0x6C, 0xFD, 0x8F, 0x1E, 0x6B, 0xFA, 0x88, 0x19,
    0x62, 0xF3, 0x81, 0x10, 0x65, 0xF4, 0x86, 0x17,
    0x48, 0xD9, 0xAB, 0x3A, 0x4F, 0xDE, 0xAC, 0x3D,
    0x46, 0xD7, 0xA5, 0x34, 0x41, 0xD0, 0xA2, 0x33,
    0x54, 0xC5, 0xB7, 0x26, 0x53, 0xC2, 0xB0, 0x21,
    0x5A, 0xCB, 0xB9, 0x28, 0x5D, 0xCC, 0xBE, 0x2F,
    0xE0, 0x71, 0x03, 0x92, 0xE7, 0x76, 0x04, 0x95,
    0xEE, 0x7F, 0x0D, 0x9C, 0xE9, 0x78, 0x0A, 0x9B,
    0xFC, 0x6D, 0x1F, 0x8E, 0xFB, 0x6A, 0x18, 0x89,
    0xF2, 0x63, 0x11, 0x80, 0xF5, 0x64, 0x16, 0x87,
    0xD8, 0x49, 0x3B, 0xAA, 0xDF, 0x4E, 0x3C, 0xAD,
    0xD6, 0x47, 0x35, 0xA4, 0xD1, 0x40, 0x32, 0xA3,
    0xC4, 0x55, 0x27, 0xB6, 0xC3, 0x52, 0x20, 0xB1,
    0xCA, 0x5B, 0x29, 0xB8, 0xCD, 0x5C, 0x2E, 0xBF,
    0x90, 0x01, 0x73, 0xE2, 0x97, 0x06, 0x74, 0xE5,
    0x9E, 0x0F, 0x7D, 0xEC, 0x99, 0x08, 0x7A, 0xEB,
    0x8C, 0x1D, 0x6F, 0xFE, 0x8B, 0x1A, 0x68, 0xF9,
    0x82, 0x13, 0x61, 0xF0, 0x85, 0x14, 0x66, 0xF7,
    0xA8, 0x39, 0x4B, 0xDA, 0xAF, 0x3E, 0x4C, 0xDD,
    0xA6, 0x37, 0x45, 0xD4, 0xA1, 0x30, 0x42, 0xD3,
    0xB4, 0x25, 0x57, 0xC6, 0xB3, 0x22, 0x50, 0xC1,
    0xBA, 0x2B, 0x59, 0xC8, 0xBD, 0x2C, 0x5E, 0xCF
};

static int computeCrc( const char *data, uint len )
{
    int sum = 0xFF;
    while ( len > 0 ) {
        sum = crcTable[ ( sum ^ *data++ ) & 0xFF ];
        --len;
    }
    return ((0xFF - sum) & 0xFF);
}


void SimRules::tryReadCommand()
{
    int len, posn;
    int channel, type;
    int temp, lasteol;

    // Read as much data as possible into "incomingBuffer".
    len = sizeof(incomingBuffer) - 1 - incomingUsed;
    len = read( incomingBuffer + incomingUsed, len );
    if ( len <= 0 ) {
        // The connection has been closed by the remote end.
        return;
    }
    incomingUsed += len;

    // Split the incoming data into GSM 07.10 packets or text lines.
    if ( useGsm0710 ) {
        // Extract GSM 07.10 packets from the incoming buffer.
        posn = 0;
        while ( posn < incomingUsed ) {
            if ( incomingBuffer[posn] == (char)0xF9 ) {

                // Skip additional 0xF9 bytes between frames.
                while ( ( posn + 1 ) < incomingUsed &&
                        incomingBuffer[posn + 1] == (char)0xF9 ) {
                    ++posn;
                }

                // We need at least 4 bytes for the header.
                if ( ( posn + 4 ) > incomingUsed )
                    break;

                // The low bits of the second and fourth bytes should be 1,
                // which indicates short channel number and length values.
                if ( ( incomingBuffer[posn + 1] & 0x01 ) == 0 ||
                     ( incomingBuffer[posn + 3] & 0x01 ) == 0 ) {
                    ++posn;
                    continue;
                }

                // Get the packet length and validate it.
                len = (incomingBuffer[posn + 3] >> 1) & 0x7F;
                if ( ( posn + 5 + len ) > incomingUsed )
                    break;

                // Verify the packet header checksum.
                if ( ( ( computeCrc( incomingBuffer + posn + 1, 3 ) ^
                         incomingBuffer[posn + len + 4] ) & 0xFF ) != 0 ) {
                    qDebug() << "*** GSM 07.10 checksum check failed ***";
                    posn += len + 5;
                    continue;
                }

                // Get the channel number and packet type from the header.
                channel = (incomingBuffer[posn + 1] >> 2) & 0x3F;
                type = incomingBuffer[posn + 2] & 0xEF;  // Strip "PF" bit.

                // Dispatch data packets to the appropriate channel.
                if ( type == 0xEF || type == 0x03 ) {
                    if ( channel == 0 ) {
                        if ( len == 2 &&
                             incomingBuffer[posn + 4] == (char)0xC3 &&
                             incomingBuffer[posn + 5] == (char)0x01 ) {
                            // This is the "terminate" commmand, which
                            // indicates that we should exit GSM 07.10 mode.
                            useGsm0710 = false;
                            posn += len + 5;
                            if ( posn < incomingUsed &&
                                 incomingBuffer[posn] == (char)0xF9 ) {
                                // Skip the trailing 0xF9 on the terminate.
                                ++posn;
                            }
                            qDebug() << "GSM 07.10 mode deactivated";
                            break;
                        }
                    } else {
                        // Ordinary data packet on a specific channel.
                        memcpy( lineBuffer + lineUsed,
                                incomingBuffer + posn + 4, len );
                        lineUsed += len;

                        // Process any complete lines that we have received.
                        lasteol = 0;
                        temp = 0;
                        currentChannel = channel;
                        while ( temp < lineUsed ) {
                            if ( lineBuffer[temp] == '\r' ) {
                                lineBuffer[temp] = '\0';
                                command( lineBuffer + lasteol );
                                ++temp;
                                if ( temp < lineUsed &&
                                     lineBuffer[temp] == '\n' ) {
                                    ++temp;
                                }
                                lasteol = temp;
                            } else if ( lineBuffer[temp] == 0x1A ) {
                                // Probably the terminator on an SMS PDU,
                                // which may or may not be followed by a CR.
                                lineBuffer[temp] = '\0';
                                command( lineBuffer + lasteol );
                                ++temp;
                                if ( temp < lineUsed &&
                                     lineBuffer[temp] == '\r' ) {
                                    ++temp;
                                }
                                lasteol = temp;
                            } else if ( lineBuffer[temp] == '\n' ) {
                                lineBuffer[temp] = '\0';
                                command( lineBuffer + lasteol );
                                ++temp;
                                lasteol = temp;
                            } else {
                                ++temp;
                            }
                        }
                        currentChannel = 1;
                        memmove( lineBuffer, lineBuffer + lasteol,
                                 lineUsed - lasteol );
                        lineUsed -= lasteol;
                    }
                }
                posn += len + 5;

            } else {
                // Skip garbage byte outside of a GSM 07.10 packet.
                ++posn;
            }
        }
        memmove( incomingBuffer, incomingBuffer + posn, incomingUsed - posn );
        incomingUsed -= posn;
        if ( !useGsm0710 )
            goto processText;   // We've just exited GSM 07.10 mode.
    } else {
        // We aren't using multi-plexing yet, so split into text lines.
    processText:
        len = 0;
        while ( len < incomingUsed ) {
            if ( incomingBuffer[len] == '\r' ) {
                if ( (len + 1) < incomingUsed &&
                     incomingBuffer[len + 1] == '\n' ) {
                    ++len;
                }
                lineBuffer[lineUsed] = '\0';
                if ( lineBuffer[0] != (char)0xF9 ) {
                    command( lineBuffer );
                }
                lineUsed = 0;
            } else if ( incomingBuffer[len] == 0x1A ) {
                // Probably the terminator on an SMS PDU,
                // which may or may not be followed by a CR.
                if ( (len + 1) < incomingUsed &&
                     incomingBuffer[len + 1] == '\r' ) {
                    ++len;
                }
                lineBuffer[lineUsed] = '\0';
                if ( lineBuffer[0] != (char)0xF9 ) {
                    command( lineBuffer );
                }
                lineUsed = 0;
            } else if ( incomingBuffer[len] == '\n' ) {
                lineBuffer[lineUsed] = '\0';
                if ( lineBuffer[0] != (char)0xF9 ) {
                    command( lineBuffer );
                }
                lineUsed = 0;
            } else if ( lineUsed < (int)( sizeof(lineBuffer) - 1 ) ) {
                lineBuffer[lineUsed++] = incomingBuffer[len];
            }
            ++len;
        }
        incomingUsed = 0;
    }
}

void SimRules::destruct()
{
    int count = simApps.count();

    for ( int i = 0; i < count; i++ )
        simApps.removeAt( 0 );

    delete conformanceApp;
    conformanceApp = NULL;
    delete defaultToolkitApp;
    defaultToolkitApp = NULL;
    toolkitApp = NULL;

    if ( getMachine() )
        getMachine()->handleNewApp();

    if ( defState )
        delete defState;
    defState = NULL;

    if ( _simAuth )
        delete _simAuth;
    _simAuth = NULL;

    if ( _callManager )
        delete _callManager;
    _callManager = NULL;

    if ( fileSystem )
        delete fileSystem;
    fileSystem = NULL;

    if (machine) machine->deleteLater();
    deleteLater();
}

void SimRules::setPhoneNumber(const QString &s)
{
    mPhoneNumber = s;

    if (machine) machine->setPhoneNumber(s);
}

HardwareManipulator * SimRules::getMachine() const
{
    return machine;
}

void SimRules::setSimApplication( SimApplication *app )
{
    if ( toolkitApp == app )
        return;

    if ( toolkitApp )
        toolkitApp->abort();

    toolkitApp = app;
}

const QList<SimApplication *> SimRules::getSimApps()
{
    return simApps;
}

void SimRules::switchTo(const QString& name)
{
    SimState *newState = state( name );
    if ( newState ) {
        if ( currentState )
            currentState->leave();
        currentState = newState;
        currentState->enter();
    }
}


SimState *SimRules::state( const QString& name ) const
{
    if ( name == "default" )
        return defaultState();

    QList<SimState *>::ConstIterator iter;
    for ( iter = states.begin(); iter != states.end(); ++iter ) {

        if ( (*iter)->name() == name ) {
            return *iter;
        }

    }
    qWarning() << "Warning: no state called \"" << name << "\" has been defined";
    return 0;
}

bool SimRules::simCsimOk( const QByteArray& payload )
{
    unsigned char sw1 = 0x90;
    unsigned char sw2 = 0x00;
    QByteArray resp = payload;

    if ( toolkitApp ) {
        QByteArray cmd = toolkitApp->fetch();
        if ( !cmd.isEmpty() ) {
            sw1 = 0x91;
            sw2 = cmd.size();
        }
    }

    resp += sw1;
    resp += sw2;
    respond( "+CSIM: " + QString::number( resp.size() * 2 ) + "," +
                           QAtUtils::toHex( resp ) + "\\n\\nOK" );

    return true;
}

bool SimRules::simCommand( const QString& cmd )
{
    // 3GPP Terminal Response Command
    if ( cmd.startsWith("AT+CUSATT=") ) {
        int start = cmd.indexOf( QChar('=') ) + 1;
        QByteArray response = QAtUtils::fromHex( cmd.mid(start) );
        QSimTerminalResponse resp = QSimTerminalResponse::fromPdu( response );

        if ( !toolkitApp || !toolkitApp->response( resp ) )
            respond( "ERROR" );

        return true;
    }

    // 3GPP Envelope command
    if ( cmd.startsWith("AT+CUSATE=") ) {
        int start = cmd.indexOf( QChar('=') ) + 1;
        QByteArray envelope = QAtUtils::fromHex( cmd.mid(start) );
        QSimEnvelope env = QSimEnvelope::fromPdu( envelope );

        if (!toolkitApp || !toolkitApp->envelope( env ) )
            respond( "ERROR" );

        return true;
    }

    // If not AT+CSIM, then this is not a SIM toolkit command.
    if ( !cmd.startsWith( "AT+CSIM=" ) )
        return false;

    if ( getMachine() && !getMachine()->getSimPresent() )
        return true;

    // Extract the binary payload of the AT+CSIM command.
    int comma = cmd.indexOf( QChar(',') );
    if ( comma < 0 )
        return false;
    QByteArray param = QAtUtils::fromHex( cmd.mid(comma + 1) );

    if ( param.length() < 4 ) {
        /* Wrong length */
        respond( "+CSIM: 4,6700\\n\\nOK" );
        return false;
    }

    if ( param[0] != (char)0xA0 ) {
        /* CLA not supported */
        respond( "+CSIM: 4,6800\\n\\nOK" );
        return false;
    }

    // Determine what kind of command we are dealing with.
    // Check for TERMINAL PROFILE, FETCH, TERMINAL RESPONSE,
    // ENVELOPE and UNBLOCK CHV packets.
    if ( param[1] == (char)0x10 ) {
        /* Abort the SIM application and force it to return to the main menu. */
        if ( toolkitApp )
            toolkitApp->abort();

        /* Download of a TERMINAL PROFILE.  We respond with a simple OK,
         * on the assumption that what we were sent was valid.  */
        return simCsimOk( QByteArray() );
    } else if ( param[1] == (char)0x12 ) {
        if ( !toolkitApp ) {
            respond( "+CSIM: 4,6F00\\n\\nOK" );
            return true;
        }

        /* Fetch the current command contents. */
        QByteArray resp = toolkitApp->fetch( true );
        if ( resp.isEmpty() ) {
            /* We weren't expecting a FETCH. */
            respond( "+CSIM: 4,6F00\\n\\nOK" );
            return true;
        }

        return simCsimOk( resp );
    } else if ( param.length() >= 5 && param[1] == (char)0x14 ) {
        if ( !toolkitApp ) {
            respond( "+CSIM: 4,6F00\\n\\nOK" );
            return true;
        }

        /* Process a TERMINAL RESPONSE message. */
        QSimTerminalResponse resp =
            QSimTerminalResponse::fromPdu( param.mid(5) );

        /* Incase of successful case, response is sent inside
         * the SimApplication::response function. response function
         * also handles the notification of new command
         */
        if ( !toolkitApp->response( resp ) )
            respond( "+CSIM: 4,6F00\\n\\nOK" );

        return true;
    } else if ( param.length() >= 5 && param[1] == (char)0x2c &&
                    param[4] == (char)0x10 && param.size() >= 21 ) {
        // UNBLOCK CHV command, for resetting a PIN using a PUK.
        QString pinName = "PINVALUE";
        QString pukName = "PUKVALUE";
        if ( param[3] == (char)0x02 ) {
            pinName = "PIN2VALUE";
            pukName = "PUK2VALUE";
        }
        QByteArray pukValue = param.mid(5, 8);
        QByteArray pinValue = param.mid(13, 8);
        while ( pukValue.size() > 0 && pukValue[pukValue.size() - 1] == (char)0xFF )
            pukValue = pukValue.left( pukValue.size() - 1 );
        while ( pinValue.size() > 0 && pinValue[pinValue.size() - 1] == (char)0xFF )
            pinValue = pinValue.left( pinValue.size() - 1 );
        if ( QString::fromUtf8( pukValue ) != variable( pukName ) ) {
            respond( "+CSIM: 4,9804\\n\\nOK" );
        } else {
            setVariable( pinName, QString::fromUtf8( pinValue ) );
            simCsimOk( QByteArray() );
        }

        return true;
    } else if ( param.length() >= 5 && param[1] == (char)0xC2 ) {
        /* ENVELOPE */
        if ( !toolkitApp ) {
            respond( "+CSIM: 4,6F00\\n\\nOK" );
            return true;
        }

        QSimEnvelope env = QSimEnvelope::fromPdu( param.mid(5) );
        if ( toolkitApp->envelope( env ) )
            return simCsimOk( QByteArray() );

        /* Envelope not supported or current command doesn't allow envelopes. */
        respond( "+CSIM: 4,6F00\\n\\nOK" );
        return true;
    } else if ( param[1] == (char)0xf2 ) {
        /* STATUS command, for now ignore the parameters */
        return simCsimOk( QByteArray() );
    }

    // Don't know this SIM command.
    respond( "+CSIM: 4,6D00\\n\\nOK" );
    return true;
}

void SimRules::command( const QString& cmd )
{
    if(getMachine())
        getMachine()->handleToData(cmd);

    // Process call-related commands with the call manager.
    if ( _callManager->command( cmd ) )
        return;

    if (_app_wrapper && _app_wrapper->command( cmd ))
        return;

    // Process SIM toolkit related commands with the current SIM application.
    if ( simCommand( cmd ) )
        return;

    if ( ! currentState->command( cmd ) ) {
        if ( cmd.startsWith( "AT+CRSM=" ) && fileSystem ) {

            // Process a filesystem access command.
            fileSystem->crsm( cmd.mid(8) );

        } else if ( cmd.startsWith( "AT+CPBS" ) ||
                    cmd.startsWith( "AT+CPBR" ) ||
                    cmd.startsWith( "AT+CPBW" ) ) {

            // Process a phonebook access command.
            phoneBook( cmd );

        } else if ( cmd.startsWith( "AT+CMUX=0," ) ) {

            // Request to turn on GSM 07.10 multiplexing.
            respond( "OK" );
            useGsm0710 = true;

        } else if ( cmd.startsWith( "AT+CPWD=\"SC\",\"" ) ) {

            // Change SIM PIN value.
            changePin( cmd );

        } else if ( cmd.startsWith( "AT" ) ) {

            // All other AT commands are not understood.
            respond( "ERROR" );

        }
    }
}

SimPhoneBook::SimPhoneBook( int size, QObject *parent )
    : QObject( parent )
{
    while ( size-- > 0 ) {
        numbers.append( QString() );
        names.append( QString() );
        hiddens.append( INVALID_VALUE_HIDDEN );
        groups.append( QString() );
        adNumbers.append( QString() );
        secondTexts.append( QString() );
        emails.append( QString() );
        sipUris.append( QString() );
        telUris.append( QString() );
    }
}

SimPhoneBook::~SimPhoneBook()
{
}

int SimPhoneBook::used() const
{
    int count = 0;
    for ( int index = 0; index < numbers.size(); ++index ) {
        if ( !numbers[index].isEmpty() )
            ++count;
    }
    return count;
}

QString SimPhoneBook::number( int index ) const
{
    if ( index >= 1 && index <= numbers.size() )
        return numbers[index - 1];
    else
        return QString();
}

QString SimPhoneBook::name( int index ) const
{
    if ( index >= 1 && index <= names.size() )
        return names[index - 1];
    else
        return QString();
}

int SimPhoneBook::hidden( int index ) const
{
    if ( index >= 1 && index <= hiddens.size() )
        return hiddens[index - 1];
    else
        return INVALID_VALUE_HIDDEN;
}

QString SimPhoneBook::group( int index ) const
{
    if ( index >= 1 && index <= groups.size() )
        return groups[index - 1];
    else
        return QString();
}

QString SimPhoneBook::adNumber( int index ) const
{
    if ( index >= 1 && index <= adNumbers.size() )
        return adNumbers[index - 1];
    else
        return QString();
}

QString SimPhoneBook::secondText( int index ) const
{
    if ( index >= 1 && index <= secondTexts.size() )
        return secondTexts[index - 1];
    else
        return QString();
}

QString SimPhoneBook::email( int index ) const
{
    if ( index >= 1 && index <= emails.size() )
        return emails[index - 1];
    else
        return QString();
}

QString SimPhoneBook::sipUri( int index ) const
{
    if ( index >= 1 && index <= sipUris.size() )
        return sipUris[index - 1];
    else
        return QString();
}

QString SimPhoneBook::telUri( int index ) const
{
    if ( index >= 1 && index <= telUris.size() )
        return telUris[index - 1];
    else
        return QString();
}

void SimPhoneBook::setDetails( int index, const QString& number,
    const QString& name, int hidden, const QString& group,
    const QString& adNumber, const QString& secondText, const QString& email,
    const QString& sipUri, const QString& telUri )
{
    if ( index >= 1 && index <= numbers.size() ) {
        numbers.replace( index - 1, number );
        names.replace( index - 1, name );
        hiddens.replace( index - 1, hidden );
        groups.replace( index - 1, group );
        adNumbers.replace( index - 1, adNumber );
        secondTexts.replace( index - 1, secondText );
        emails.replace( index - 1, email );
        sipUris.replace( index - 1, sipUri );
        telUris.replace( index - 1, telUri );
    }
}

void SimRules::initPhoneBooks()
{
    currentPhoneBook = "SM";
    phoneBooks.insert( "SM", new SimPhoneBook( 150, this ) );
}

void SimRules::loadPhoneBook( SimXmlNode& node )
{
    QString name = node.getAttribute( "name" );
    int size = node.getAttribute( "size" ).toInt();
    if ( !phoneBooks.contains( name ) ) {
        phoneBooks.insert( name, new SimPhoneBook( size, this ) );
    }
    SimPhoneBook *pb = phoneBooks[name];
    SimXmlNode *n = node.children;
    while ( n != 0 ) {
        if ( n->tag == "entry" ) {
            // Load a phone book entry.
            int index = n->getAttribute( "index" ).toInt();
            QString number = n->getAttribute( "number" );
            QString name = n->getAttribute( "name" );
            QString hiddenString = n->getAttribute( "hidden" );
            int hidden;
            if ( hiddenString.isEmpty() )
                hidden = INVALID_VALUE_HIDDEN;
            else
                hidden = hiddenString.toInt();
            QString group = n->getAttribute( "group" );
            QString adNumber = n->getAttribute( "adnumber" );
            QString secondText = n->getAttribute( "secondtext" );
            QString email = n->getAttribute( "email" );
            QString sipUri = n->getAttribute( "sip_uri" );
            QString telUri = n->getAttribute( "tel_uri" );
            pb->setDetails( index, number, name, hidden, group, adNumber,
                            secondText, email, sipUri, telUri );
        }
        n = n->next;
    }
}

QString SimRules::convertCharset( const QString& str )
{
    if ( variables["SCS"] == "UCS2" ) {
        static const char hexchars[] = "0123456789ABCDEF";
        const QChar *c = str.unicode();
        int length = str.length();
        QString s;
        while ( length-- > 0 ) {
            uint ch = c->unicode();
            ++c;
            s += hexchars[ (ch >> 12) & 0x0F ];
            s += hexchars[ (ch >> 8) & 0x0F ];
            s += hexchars[ (ch >> 4) & 0x0F ];
            s += hexchars[ ch & 0x0F ];
        }
        return s;
    } else {
        return str;
    }
}

void SimRules::phoneBook( const QString& cmd )
{
    SimPhoneBook *pb = currentPB();
    if ( !pb )
        return;

    // If the SIM PIN is not ready, then disable the phone books.
    if ( variable("PINNAME") != "READY" ) {
        respond( "ERROR" );
        return;
    }

    if ( cmd.startsWith( "AT+CPBS=?" ) ) {
        QStringList names = phoneBooks.keys();
        QString response = "+CPBS: (";
        foreach ( QString name, names ) {
            if ( response.length() > 8 )
                response += QChar(',');
            response += "\"" + name + "\"";
        }
        response += ")\\n\\nOK";
        respond( response );
    } else if ( cmd.startsWith( "AT+CPBS?" ) ) {
        respond( "+CPBS: \"" + currentPhoneBook + "\"," +
                 QString::number( pb->used() ) + "," +
                 QString::number( pb->size() ) + "\\n\\nOK" );
    } else if ( cmd.startsWith( "AT+CPBS=\"" ) ) {
        QString name = cmd.mid(9).left(2);
        if ( phoneBooks.contains( name ) ) {
            // If a password is supplied, then check it against PIN2VALUE.
            int comma = cmd.indexOf( QChar(',') );
            if ( comma >= 0 ) {
                QString password = cmd.mid(comma + 1);
                password.remove( QChar('"') );
                if ( password != variable( "PIN2VALUE" ) ) {
                    respond( "ERROR" );
                    return;
                }
            }
            currentPhoneBook = name;
            respond( "OK" );
        } else {
            // Invalid phone book name.
            respond( "ERROR" );
        }
    } else if ( cmd.startsWith( "AT+CPBR=?" ) ) {
        respond( "+CPBR: (1-" + QString::number( pb->size() ) + ")"
                 + "," + QString::number( PHONEBOOK_NLENGTH )
                 + "," + QString::number( PHONEBOOK_TLENGTH )
                 + "," + QString::number( PHONEBOOK_GLENGTH )
                 + "," + QString::number( PHONEBOOK_SLENGTH )
                 + "," + QString::number( PHONEBOOK_ELENGTH )
                 + "," + QString::number( PHONEBOOK_SIPLENGTH )
                 + "," + QString::number( PHONEBOOK_TELLENGTH ) + "\\n\\nOK");
    } else if ( cmd.startsWith( "AT+CPBR=" ) ) {
        QString args = cmd.mid(8);
        int comma = args.indexOf( QChar(',') );
        int first, last;
        if ( comma < 0 ) {
            // Read one entry.
            first = args.toInt();
            last = first;
        } else {
            // Read a range of entries.
            first = args.left(comma).toInt();
            last = args.mid(comma + 1).toInt();
        }
        while ( first <= last ) {
            QString number = pb->number( first );
            QString name = convertCharset( pb ->name( first ) );
            int hidden = pb->hidden( first );
            QString group = convertCharset( pb->group( first ) );
            QString adNumber = pb->adNumber( first );
            QString secondText = convertCharset( pb->secondText( first ) );
            QString email = convertCharset( pb->email( first ) );
            QString sipUri = convertCharset( pb->sipUri( first ) );
            QString telUri = convertCharset( pb->telUri( first ) );
            if ( !number.isEmpty() ) {
                QString s = "+CPBR: " + QString::number( first ) + "," +
                         QAtUtils::encodeNumber( number ) + ",\"" +
                         QAtUtils::quote( name ) + "\"";
                if (hidden != INVALID_VALUE_HIDDEN) {
                    s += "," + QString::number( hidden );
                } else
                    goto out;
                if ( !group.isEmpty() ) {
                    s += ",\"" + QAtUtils::quote( group ) + "\"";
                } else
                    goto out;
                if ( !adNumber.isEmpty() ) {
                    s += "," + QAtUtils::encodeNumber( adNumber );
                } else
                    goto out;
                if ( !secondText.isEmpty() ) {
                    s += ",\"" + QAtUtils::quote( secondText ) + "\"";
                } else
                    goto out;
                if ( !email.isEmpty() ) {
                    s += ",\"" + QAtUtils::quote( email ) + "\"";
                } else
                    goto out;
                if ( !sipUri.isEmpty() ) {
                    s += ",\"" + QAtUtils::quote( sipUri ) + "\"";
                } else
                    goto out;
                if ( !telUri.isEmpty() ) {
                    s += ",\"" + QAtUtils::quote( telUri ) + "\"";
                } else
                    goto out;

out:
                respond( s );
            }
            ++first;
        }
        respond( "OK" );
    } else if ( cmd.startsWith( "AT+CPBW=" ) ) {
        uint posn = 8;
        int index = (int)QAtUtils::parseNumber( cmd, posn );
        if ( index < 1 || index > pb->size() ) {
            // Invalid index.
            respond( "ERROR" );
            return;
        }
        if ( ((int)posn) >= cmd.length() ) {
            // Delete an entry from the phone book.
            pb->setDetails( index, QString(), QString() );
        } else {
            // Write new details to an entry.
            QString number = QAtUtils::nextString( cmd, posn );
            uint type = QAtUtils::parseNumber( cmd, posn );
            QString name = QAtUtils::nextString( cmd, posn );
            number = QAtUtils::decodeNumber( number, type );
            QString group = QAtUtils::nextString( cmd, posn );
            QString adNumber = QAtUtils::nextString( cmd, posn );
            uint adType = QAtUtils::parseNumber( cmd, posn );
            adNumber = QAtUtils::decodeNumber( adNumber, adType );
            QString secondText = QAtUtils::nextString( cmd, posn );
            QString email = QAtUtils::nextString( cmd, posn );
            QString sipUri = QAtUtils::nextString( cmd, posn );
            QString telUri = QAtUtils::nextString( cmd, posn );
            int hidden = QAtUtils::parseNumber( cmd, posn, INVALID_VALUE_HIDDEN);
            if ( number.length() > PHONEBOOK_NLENGTH ||
                 name.length() > PHONEBOOK_TLENGTH ||
                 group.length() > PHONEBOOK_GLENGTH ||
                 adNumber.length() > PHONEBOOK_NLENGTH ||
                 secondText.length() > PHONEBOOK_SLENGTH ||
                 email.length() > PHONEBOOK_ELENGTH ||
                 sipUri.length() > PHONEBOOK_SIPLENGTH ||
                 telUri.length() > PHONEBOOK_TELLENGTH ) {
                 respond( "ERROR" );
                 return;
            }
            pb->setDetails( index, number, name, hidden, group,
                            adNumber, secondText, email, sipUri, telUri );
        }
        respond( "OK" );
    } else {
        respond( "ERROR" );
    }
}

void SimRules::changePin( const QString& cmd )
{
    QStringList parts = cmd.split(QChar('"'));
    if (parts.size() < 6) {
        respond( "ERROR" );
        return;
    }
    QString oldPin = parts[3];
    QString newPin = parts[5];
    if ( variable( "PINVALUE" ) != oldPin ) {
        respond( "ERROR" );
        return;
    }
    if ( newPin.size() < 4 || newPin.size() > 8 ) {
        respond( "ERROR" );
        return;
    }
    setVariable( "PINVALUE", newPin );
    respond( "OK" );
}

SimPhoneBook *SimRules::currentPB() const
{
    if ( phoneBooks.contains( currentPhoneBook ) )
        return phoneBooks[currentPhoneBook];
    else
        return 0;
}

int SimRules::newCall()
{
    int id;
    for( id = 1; id <= 8; ++id ) {
        if ( ( usedCallIds & (1 << id) ) == 0 ) {
            break;
        }
    }
    usedCallIds |= (1 << id);
    return id;
}


void SimRules::forgetCall( int id )
{
    usedCallIds &= ~(1 << id);
}


void SimRules::forgetAllCalls()
{
    usedCallIds = 0;
}

QString expandEscapes( const QString& data, bool eol )
{
    // Expand escapes and end of line markers in the data.
    static char const escapes[] = "\a\bcde\fghijklm\nopq\rs\tu\vwxyz";
    QByteArray res;
    QByteArray buffer = data.toUtf8();
    const char *buf = buffer.data();
    int ch;
    int prevch = 0;

    res += ( '\r' );
    res += ( '\n' );

    while ( ( ch = *buf++ ) != '\0' ) {
        if ( ch == '\n' ) {
            res += ( '\r' );
            res += ( '\n' );
        } else if ( ch == '\\' ) {
            ch = *buf++;
            if ( ch == '\0' ) {
                res += ( '\\' );
                break;
            } else if ( ch == 'n' ) {
                res += ( '\r' );
                res += ( '\n' );
                ch = '\n';
            } else if ( ch >= 'a' && ch <= 'z' ) {
                ch = escapes[ch - 'a'];
                res += ( ch );
            } else {
                res += ( '\\' );
                res += ( ch );
            }
        } else if ( ch != '\r' ) {
            res += ( ch );
        }
        prevch = ch;
    }
    if ( prevch != '\n' && eol ) {
        res += ( '\r' );
        res += ( '\n' );
    }

    return QString::fromUtf8(res.data());
}


void SimRules::respond( const QString& resp, int delay, bool eol )
{
    QString r = expand( resp );
    QByteArray escaped = expandEscapes( r, eol ).toUtf8();

    if ( !delay ) {
        writeChatData(escaped.data(), escaped.length());
        flush();
    } else {
        SimDelayTimer *timer = new SimDelayTimer( escaped, currentChannel );
        timer->setSingleShot( true );
        connect(timer,SIGNAL(timeout()),this,SLOT(delayTimeout()));
        timer->start( delay );
    }
    if(getMachine())
        getMachine()->handleFromData(QString(escaped));
}

void SimRules::proactiveCommandNotify( const QByteArray& cmd )
{
    unsolicited( "+CUSATP: " + QAtUtils::toHex( cmd ) );
}

void SimRules::modemHandledCommandNotify( const QByteArray& cmd )
{
    unsolicited( "*HCMD: " + QAtUtils::toHex( cmd ) );
}

void SimRules::callControlEventNotify( const QSimControlEvent& evt )
{
    unsolicited( "*TCC: " + QString::number( (int) (evt.type()) ) +
              "," + QAtUtils::toHex( evt.toPdu() ) );
}

void SimRules::delayTimeout()
{
    SimDelayTimer *timer = (SimDelayTimer *)sender();
    int save = currentChannel;
    currentChannel = timer->channel;
    writeChatData(timer->response.toLatin1().data(), timer->response.length());
    flush();
    currentChannel = save;
    timer->deleteLater();
}

void SimRules::delaySetVariable()
{
    QVariantTimer *timer = (QVariantTimer *)sender();
    QPairKV kv = timer->param.value<QPairKV>();

    setVariable( kv.first, kv.second );

    delete timer;
}

void SimRules::dialCheck( const QString& number, bool& ok )
{
    // Bail out if the fixed-dialing phone book is not active or present.
    if ( variable("FD") != "1" )
        return;
    if ( !phoneBooks.contains( "FD" ) ) {
        ok = false;
        return;
    }

    // The dial is OK if the number starts with an existing number in "FD".
    for( int i = 1; i <= phoneBooks["FD"]->used(); i++ ){
        if( number.startsWith(phoneBooks["FD"]->number(i)) ){
            ok = true;
            return;
        }
        ok = false;
    }

    // The dial is OK if it is one of the standard emergency numbers.
    if (number == "112" || number == "911" || number == "08" || number == "000") {
        ok = true;
    }
}

void SimRules::unsolicited( const QString& resp )
{
    QString r = expand( resp );

    QByteArray escaped = expandEscapes( r, true ).toUtf8();
    writeChatData( escaped , escaped.length() );
    flush();
}


void SimRules::writeGsmFrame( int type, const char *data, uint len )
{
    char frame[MAX_GSM0710_FRAME_SIZE + 6];
    frame[0] = (char)0xF9;
    frame[1] = (char)((currentChannel << 2) | 0x03);
    frame[2] = (char)type;
    frame[3] = (char)((len << 1) | 0x01);
    if ( len > 0 )
        memcpy( frame + 4, data, len);
    // Note: GSM 07.10 says that the CRC is only computed over the header.
    frame[len + 4] = (char)computeCrc( frame + 1, 3 );
    frame[len + 5] = (char)0xF9;
    write( frame, len + 6 );
}


void SimRules::writeChatData( const char *data, uint len )
{
    if ( !isOpen() )
        return;
    if ( !useGsm0710 ) {
        // We aren't using multi-plexing at present.
        write( data, len );
    } else {
        // Format GSM 07.10 frames and send them via the current channel.
        uint templen;
        while ( len > 0 ) {
            templen = len;
            if ( templen > MAX_GSM0710_FRAME_SIZE ) {
                templen = MAX_GSM0710_FRAME_SIZE;
            }
            writeGsmFrame( 0xEF, data, templen );
            data += templen;
            len -= templen;
        }
    }
}


QString SimRules::expand( const QString& s )
{
    int prev, index, len, start, end;
    QString result;
    QString name;

    index = s.indexOf( QChar('$') );
    if ( index == -1 )
        return s;

    prev = 0;
    len = s.length();
    do {
        result += s.mid( prev, index - prev );
        ++index;
        if ( index < len && s[index] == '{' ) {
            ++index;
            start = index;
            end = s.indexOf( QChar('}'), index );
            if ( end == -1 ) {
                end = len;
                index = len;
            } else {
                index = end + 1;
            }
            name = s.mid( start, end - start );
            result += variable(name);
        } else {
            result += "$";
        }
        prev = index;
        index = s.indexOf( QChar('$'), index );
    } while ( index != -1 );
    result += s.mid( prev );
    return result;
}

void SimRules::queryVariable( const QString &name )
{
    emit returnQueryVariable( name, variable(name) );
}

void SimRules::queryState( )
{
    if (currentState)
        emit returnQueryState( currentState->name() );
    else
        emit returnQueryState( QString() );
}

void SimRules::setVariable( const QString& name, const QString& value )
{
        variables[name] = expand(value);
}

QString SimRules::variable( const QString& name )
{
    return variables[name];

}
