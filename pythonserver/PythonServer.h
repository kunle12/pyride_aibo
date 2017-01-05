#ifndef PythonServer_h_DEFINED
#define PythonServer_h_DEFINED

#include <OPENR/OObject.h>
#include <OPENR/OSubject.h>
#include <OPENR/OObserver.h>
#include <ant.h>
#include "def.h"

#include <string>
#include <deque>

#include "Python.h"
#include "PyConnectStub.h"
#include "PyConnectInterObjComm.h"
#include "PyConnectInterNetComm.h"

// define python config
#define PYTHONSERVER_BUFFER_SIZE  2048
#define PS_SEND_BUFFER_SIZE       PYTHONSERVER_BUFFER_SIZE*2
#define PS_RECEIVE_BUFFER_SIZE    PYTHONSERVER_BUFFER_SIZE
#define PYTHON_SERVER_PORT        27005

#define DEFAULT_PYTHON_SCRIPT_PATH "/MS/OPEN-R/Python"

// define telnet protocol 
#define TELNET_ECHO     1
#define TELNET_LINEMODE 34
#define TELNET_SE       240
#define TELNET_SB       250
#define TELNET_WILL     251
#define TELNET_WONT     252
#define TELNET_DO       253
#define TELNET_DONT     254
#define TELNET_IAC      255

#define ERASE_EOL       "\033[K"

#define KEY_CTRL_A      1
#define KEY_CTRL_C      3
#define KEY_CTRL_D      4
#define KEY_CTRL_E      5
#define KEY_CTRL_G      7
#define KEY_BACKSPACE   8
#define KEY_HTAB        9
#define KEY_DEL         127
#define KEY_ENTER       13
#define KEY_ESC         27

#define PYTHON_SESSION_MAX 10
#define TERMINAL_SIZE   80
#define MAX_HISTORY_COMMAND      20
#define PYRIDE_MAIN_SCRIPT_NAME  "py_main"

using namespace pyconnect;

typedef enum {
  SESSION_INVALID, // session is in an invalid state
  SESSION_CLOSED, // session is close not accept anything
  SESSION_READY,  // ready to accept new connection.
  SESSION_CONNECTED,
  SESSION_SENDING,
  SESSION_RECEIVING,
} SessionState;

class PythonSession;

class PythonServer : public OObject, public PyOutputWriter
{
public:
  PythonServer();
  virtual ~PythonServer() {}

  OSubject*   subject[numOfSubject];
  OObserver*  observer[numOfObserver];     

  PYCONNECT_INTEROBJCOMM_DECLARE;
  PYCONNECT_INTERNETCOMM_DECLARE;

  virtual OStatus DoInit( const OSystemEvent& event );
  virtual OStatus DoStart( const OSystemEvent& event );
  virtual OStatus DoStop( const OSystemEvent& event );
  virtual OStatus DoDestroy( const OSystemEvent& event );

  void ListenCont( ANTENVMSG msg );
  void SendCont( ANTENVMSG msg );
  void ReceiveCont( ANTENVMSG msg );
  void CloseCont( ANTENVMSG msg );
  
  void write( const char * msg );
  PyObject * mainScript() { return pMainModule_; }
  void directOutputTo( int session ) { activeSession_ = session; }
  static bool  RunMyString( const char * command );
  static void processInput( char * recData, int bytesReceived );
  MessageProcessor * getMP() { return pPyConnectStub_; }

  static  PythonServer * instance() { return s_pPythonServer; }

private:  
  PyObject *    prevStderr_;
  PyObject *    prevStdout_;
  PyObject *    pSysModule_;
  PyObject *    pMainModule_;
  PyObject *    pPyConnect_;
  PyConnectStub *  pPyConnectStub_;

  static PythonServer *  s_pPythonServer;

  antStackRef    ipstackRef_;
  PythonSession  * sessions_[PYTHON_SESSION_MAX];
  int activeSession_;

  void initPyConnect();

  friend class PythonSession;
};


class PythonSession
{
public:
  PythonSession( int id, PythonServer * server, 
            std::string & welcomeStr );
  ~PythonSession();

  int handleInput( int byteReceived );
  void write( const char * str );
  void writePrompt();

  int  state() { return sessionState_; }
  void connectReady();
  void expectInput( bool flag = true );

  bool Listen();
  void Send( const char * str );
  void Receive();
  void Close();

private:
  int id_;
  PythonServer * server_;
  bool telnetSubnegotiation_;
  antModuleRef endpoint_;
  SessionState sessionState_;

  // send buffer
  antSharedBuffer sendBuffer_;
  byte * sendData_;
  int sendSize_;
    
  // receive buffer
  antSharedBuffer  recvBuffer_;
  byte*      recvData_;
  int        recvSize_;

  std::string    welcomeStr_;
  std::string    promptStr_;

  std::deque<unsigned char> readBuffer_;
  std::deque<std::string> historyBuffer_;
  std::string currentLine_;
  std::string currentCommand_;
  int historyPos_;
  unsigned int charPos_;
  std::string multiline_;

  bool  handleTelnetCommand();
  bool  handleVTCommand();
  void  handleLine();
  void  handleDel();
  void  handleChar();
  void  handleTab();
  void  handleUp();
  void  handleDown();
  void  handleLeft();
  void  handleRight();
  void  closeSession( bool newConn = true );  
};

#endif // PythonServer_h_DEFINED
