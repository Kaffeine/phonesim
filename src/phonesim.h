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

#ifndef PHONESIM_H
#define PHONESIM_H

#include <qstring.h>
#include <qstringlist.h>
#include <qlist.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qxmlstream.h>
#include <qtcpsocket.h>
#include <qapplication.h>
#include <qmap.h>
#include <qtimer.h>
#include <qpointer.h>
#include <qsimcontrolevent.h>

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>


class SimState;
class SimItem;
class SimChat;
class SimUnsolicited;
class SimRules;
class SimFileSystem;
class CallManager;
class SimApplication;
class SimAuth;
class AidApplication;
class AidAppWrapper;


class SimXmlNode
{
public:
    SimXmlNode( const QString& tag );
    ~SimXmlNode();

    SimXmlNode *parent, *next, *children, *attributes;
    QString tag;
    QString contents;

    void addChild( SimXmlNode *child );
    void addAttribute( SimXmlNode *child );
    QString getAttribute( const QString& name );
};


class SimXmlHandler
{
public:
    SimXmlHandler();
    ~SimXmlHandler();

    bool startElement( const QString& name, const QXmlStreamAttributes& atts );
    bool endElement();
    bool characters( const QString& ch );

    SimXmlNode *documentElement() const;

private:
    SimXmlNode *tree;
    SimXmlNode *current;
};


class SimState
{
    friend class SimRules;
public:
    SimState( SimRules *rules, SimXmlNode& e );
    ~SimState() {}

    // Get the rules object that contains this state.
    SimRules *rules() const { return _rules; }

    // Get the name of this state.
    QString name() const { return _name; }

    // Enter this state, and enable unsolicited events.
    void enter();

    // Leave this state, after disabling unsolicited events.
    void leave();

    // Handle a command.  Returns false if the command was not understood.
    bool command( const QString& cmd );

private:
    QPointer<SimRules> _rules;
    QString _name;
    QList<SimItem *> items;

};


class SimItem : public QObject
{
    Q_OBJECT
public:
    SimItem( SimState *state ) { _state = state; }
    virtual ~SimItem() {}

    // Get the state that contains this item.
    SimState *state() { return _state; }

    // Receive notification of the item's state being entered.
    virtual void enter() {}

    // Receive notification of the item's state being left.
    virtual void leave() {}

    // Attempt to handle a command.  Returns false if not recognised.
    virtual bool command( const QString& ) { return false; }

private:
    SimState *_state;

};


class SimChat : public SimItem
{
    Q_OBJECT
public:
    SimChat( SimState *state, SimXmlNode& e );
    ~SimChat() {}

    virtual bool command( const QString& cmd );

private:
    QString _command;
    QString response;
    int responseDelay;
    QString switchTo;
    bool wildcard;
    bool eol;
    QStringList variables;
    QStringList values;
    QMap<QString, int> delays;
    QString newCallVar;
    QString forgetCallId;
    bool listSMS;
    bool deleteSMS;
    bool readSMS;
};


class SimUnsolicited : public SimItem
{
    Q_OBJECT
public:
    SimUnsolicited( SimState *state, SimXmlNode& e );
    ~SimUnsolicited() {}

    virtual void enter();
    virtual void leave();

private:
    QString response;
    int responseDelay;
    QString switchTo;
    bool doOnce;
    bool done;
    QTimer *timer;

private slots:
    void timeout();

};


class SimPhoneBook : public QObject
{
    Q_OBJECT
public:
    SimPhoneBook( int size, QObject *parent );
    ~SimPhoneBook();

    int size() const { return numbers.size(); }
    int used() const;

    QString number( int index ) const;
    QString name( int index ) const;
    int hidden( int index ) const;
    QString group( int index ) const;
    QString adNumber( int index ) const;
    QString secondText( int index ) const;
    QString email( int index ) const;
    QString sipUri( int index ) const;
    QString telUri( int index ) const;

    void setDetails( int index, const QString& number, const QString& name,
                     int hidden = -1, const QString& group = QString(),
                     const QString& adNumber = QString(),
                     const QString& secondText = QString(),
                     const QString& email = QString(),
                     const QString& sipUri = QString(),
                     const QString& telUri = QString() );

    bool contains( const QString& number ) const { return numbers.contains( number ); }

private:
    QStringList numbers;
    QStringList names;
    QList<int> hiddens;
    QStringList groups;
    QStringList adNumbers;
    QStringList secondTexts;
    QStringList emails;
    QStringList sipUris;
    QStringList telUris;
};

class HardwareManipulatorFactory;
class HardwareManipulator;
class SimRules : public QTcpSocket
{
    Q_OBJECT
public:
    SimRules(int fd, QObject *parent, const QString& filename, HardwareManipulatorFactory *hmf );
    ~SimRules() {}

    // get the variable value for.
    QString variable(const QString &name);

    // Get the current simulator state.
    SimState *current() const { return currentState; }

    // Get the default simulator state.
    SimState *defaultState() const { return defState; }

    // Issue a response to the client.
    void respond( const QString& resp, int delay, bool eol=true );

    // Expand variable references in a string.
    QString expand( const QString& s );

    // Allocate a new call identifier.
    int newCall();

    // Forget a call identifier.
    void forgetCall( int id );

    // Forget all call identifiers (global hangup).
    void forgetAllCalls();

    void setPhoneNumber(const QString &s);

    // Gets the hardware manipulator
    HardwareManipulator * getMachine() const;

    // Get the call manager for this rule object.
    CallManager *callManager() const { return _callManager; }

    // Get or set the SIM toolkit application.
    SimApplication *simApplication() const { return toolkitApp; }
    void setSimApplication( SimApplication *app );

    const QList<SimApplication *> getSimApps();

signals:
    void returnQueryVariable( const QString&, const QString & );
    void returnQueryState( const QString& );
    void modemCommand( const QString& );
    void modemResponse( const QString& );

public slots:
    void queryVariable( const QString &name );
    void queryState( );
    // Set a variable to a new value.
    void setVariable( const QString& name, const QString& value );

    // Switch to a new simulator state.
    void switchTo(const QString& name);

    // Process a command.
    void command( const QString& cmd );

    // Send an unsolicited response to the client.
    void unsolicited( const QString& resp );

    // Send a response line.
    void respond( const QString& resp ) { respond( resp, 0 ); }

    void proactiveCommandNotify( const QByteArray& cmd );
    void modemHandledCommandNotify( const QByteArray& cmd );
    void callControlEventNotify( const QSimControlEvent& event );

private slots:
    void tryReadCommand();
    void destruct();
    void delayTimeout();
    void delaySetVariable();
    void dialCheck( const QString& number, bool& ok );

private:
    SimState *currentState;
    SimState *defState;
    QList<SimState *> states;
    QMap<QString,QString> variables;
    int usedCallIds;
    bool useGsm0710;
    int currentChannel;
    char incomingBuffer[1024];
    int incomingUsed;
    char lineBuffer[1024];
    int lineUsed;
    SimFileSystem *fileSystem;
    SimApplication *defaultToolkitApp;
    SimApplication *toolkitApp;
    SimApplication *conformanceApp;
    QList<SimApplication *> simApps;

    // Get a particular state object.
    SimState *state( const QString& name ) const;

    QString mPhoneNumber;
    HardwareManipulator *machine;

    void writeGsmFrame( int type, const char *data, uint len );
    void writeChatData( const char *data, uint len );

    QString convertCharset( const QString& s );
    void initPhoneBooks();
    void phoneBook( const QString& cmd );
    bool simCommand( const QString& cmd );
    void changePin( const QString& cmd );
    SimPhoneBook *currentPB() const;
    void loadPhoneBook( SimXmlNode& node );

    QString currentPhoneBook;
    QMap< QString, SimPhoneBook * > phoneBooks;

    CallManager *_callManager;
    SimAuth *_simAuth;
    QList<AidApplication *> _applications;
    AidAppWrapper *_app_wrapper;

    bool simCsimOk( const QByteArray& payload );
};


class SimDelayTimer : public QTimer
{
    Q_OBJECT
public:
    SimDelayTimer( const QString& response, int channel )
        : QTimer() { this->response = response; this->channel = channel; }

public:
    QString response;
    int channel;
};

class QVariantTimer : public QTimer
{
    Q_OBJECT
public:
    QVariantTimer( QObject *parent = 0 ) : QTimer(parent) { }
    QVariant param;
};

typedef QPair<QString,QString> QPairKV;

Q_DECLARE_METATYPE(QPairKV);

#endif
