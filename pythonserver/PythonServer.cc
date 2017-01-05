#include <string.h>
#include <OPENR/OSyslog.h>
#include <OPENR/OPENRAPI.h>
#include <OPENR/core_macro.h>
#include <ant.h>
#include <EndpointTypes.h>
#include <TCPEndpointMsg.h>
#include "PythonServer.h"
#include "entry.h"

using namespace pyconnect;

PythonServer *  PythonServer::s_pPythonServer = NULL;

extern int errno;

PythonServer::PythonServer() :
  pSysModule_( NULL ),
  pMainModule_( NULL ),
  pPyConnect_( NULL ),
  pPyConnectStub_( NULL ),
  activeSession_( -1 )
{
}

OStatus PythonServer::DoInit( const OSystemEvent& event )
{
  NEW_ALL_SUBJECT_AND_OBSERVER;
  REGISTER_ALL_ENTRY;
  SET_ALL_READY_AND_NOTIFY_ENTRY;

  s_pPythonServer = this;

  // initialise Python interpreter
  Py_SetPythonHome( "/MS/OPEN-R/MW/Python" );
  Py_Initialize();
  initPyConnect();

  return oSUCCESS;
}

OStatus PythonServer::DoStart( const OSystemEvent& event )
{
  ipstackRef_ = antStackRef( "IPStack" );

  PYCONNECT_INTEROBJCOMM_INIT;
  PYCONNECT_INTERNETCOMM_INIT;

  //DEBUG_MSG( "PythonServer::DoStart()\n" );
  pSysModule_ = PyImport_ImportModule( "sys" );

  if (!pSysModule_) {
    ERROR_MSG( "PythonServer: Failed to import sys module\n" );
    return oFAIL;
  }
  
  prevStderr_ = PyObject_GetAttrString( pSysModule_, "stderr" );
  prevStdout_ = PyObject_GetAttrString( pSysModule_, "stdout" );

  PyObject_SetAttrString( pSysModule_, "stderr", pPyConnect_ );
  PyObject_SetAttrString( pSysModule_, "stdout", pPyConnect_ );
  
  char * versionStr = strdup( Py_GetVersion() );
  std::string welcomeStr = "Welcome to AIBO Python server [Python version ";
  welcomeStr = welcomeStr + strtok( versionStr, " " );
  welcomeStr = welcomeStr + "]";
  free( versionStr );
  
  PyObject * mainModule = PyImport_AddModule( "__main__" );
  if (!mainModule) {
    // we are in deep trouble should abort
    ERROR_MSG( "%s", "PythonServer failed to import __main__ module" );
    return oFAIL;;
  }

  // run main script
  PyObject * mainDict = PyModule_GetDict( mainModule );

  pMainModule_ = PyImport_ImportModuleEx( PYRIDE_MAIN_SCRIPT_NAME, mainDict, mainDict, NULL );

  if (pMainModule_) {
    PyObject_SetAttrString( mainModule, PYRIDE_MAIN_SCRIPT_NAME, pMainModule_ );
    PyObject * mainFn = PyObject_GetAttrString( pMainModule_, "main" );
    if (mainFn && PyCallable_Check( mainFn )) {
      PyObject * pResult = PyObject_CallObject( mainFn, NULL );
      Py_XDECREF( pResult );
    }
    else {
      ERROR_MSG( "PythonServer: missing main function in script %s\n",
                 PYRIDE_MAIN_SCRIPT_NAME );
    }
  }
  else {
    if (PyErr_Occurred() == PyExc_ImportError) {
      INFO_MSG( "PythonServer: No main script %s\n",
                 PYRIDE_MAIN_SCRIPT_NAME );
    }
    else {
      ERROR_MSG( "PythonServer: Failed to import main script"
                 " %s\n", PYRIDE_MAIN_SCRIPT_NAME );
      PyErr_Print();
    }
    PyErr_Clear();
  }

  for (int index = 0; index < PYTHON_SESSION_MAX; index++) {
    sessions_[index] = new PythonSession( index, this, welcomeStr ) ;
    if (sessions_[index]->state() == SESSION_INVALID) return oFAIL;
  }

  pPyConnectStub_->sendDiscoveryMsg();

  ENABLE_ALL_SUBJECT;
  ASSERT_READY_TO_ALL_OBSERVER;
  return oSUCCESS;
}    

OStatus PythonServer::DoStop( const OSystemEvent& event )
{
  for (int index = 0; index < PYTHON_SESSION_MAX; index++) {
    PythonSession * session = sessions_[index];
    delete session;
  }

  Py_XDECREF( pPyConnect_ );
  PyConnectStub::fini();

  if (prevStderr_)
  {
    PyObject_SetAttrString( pSysModule_, "stderr", prevStderr_ );
    Py_DECREF( prevStderr_ );
    prevStderr_ = NULL;
  }

  if (prevStdout_)
  {
    PyObject_SetAttrString( pSysModule_, "stdout", prevStdout_ );
    Py_DECREF( prevStdout_ );
    prevStdout_ = NULL;
  }
  Py_DECREF( pSysModule_ );
  pSysModule_ = NULL;


  PYCONNECT_INTERNETCOMM_FINI;
  PYCONNECT_INTEROBJCOMM_FINI;
  DISABLE_ALL_SUBJECT;
  DEASSERT_READY_TO_ALL_OBSERVER;
 
  return oSUCCESS;
}

OStatus PythonServer::DoDestroy( const OSystemEvent& event )
{
  DELETE_ALL_SUBJECT_AND_OBSERVER;
  Py_Finalize();
  return oSUCCESS;
}

void PythonServer::ListenCont( ANTENVMSG msg )
{
  TCPEndpointListenMsg* listenMsg
        = (TCPEndpointListenMsg*)antEnvMsg::Receive( msg );
  
  int index = (int)listenMsg->continuation;
    
  if (listenMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PythonSession::ListenCont()",
       "FAILED. listenMsg->error", listenMsg->error );
    sessions_[index]->Close();
    return;
  }

  sessions_[index]->connectReady();
}

void PythonServer::CloseCont( ANTENVMSG msg )
{
  //DEBUG_MSG( "PythonServer::CloseCont()\n" );
  
  //TCPEndpointCloseMsg* closeMsg
  //    = (TCPEndpointCloseMsg*)antEnvMsg::Receive( msg );
  //int index = (int)(closeMsg->continuation);
}

void PythonServer::SendCont( ANTENVMSG msg )
{
  TCPEndpointSendMsg* sendMsg =
    (TCPEndpointSendMsg*)antEnvMsg::Receive( msg );
  int index = (int)(sendMsg->continuation);

  if (sendMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PythonServer::SendCont()",
              "FAILED. sendMsg->error", sendMsg->error );
    sessions_[index]->Close();
    return;
  }
}

void PythonServer::ReceiveCont( ANTENVMSG msg )
{
  TCPEndpointReceiveMsg* receiveMsg
      = (TCPEndpointReceiveMsg*)antEnvMsg::Receive( msg );
  int index = (int)(receiveMsg->continuation);

  if (receiveMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PythonServer::ReceiveCont()",
              "FAILED. receiveMsg->error", receiveMsg->error );
    sessions_[index]->Close();
    return;
  }

  // process message
  sessions_[index]->handleInput( receiveMsg->sizeMin );
}

bool PythonServer::RunMyString( const char * command )
{
  // grab thread lock
  DEBUG_MSG( "Run command string: %s\n", command );

  PyObject * mainModule = PyImport_AddModule( "__main__" );
  if (!mainModule) {
    // we are in deep trouble should abort
    ERROR_MSG( "%s", "PythonServer failed to import __main__ module" );
    return false;
  }
  PyObject * mainDict = PyModule_GetDict( mainModule );

  PyObject * ret = PyRun_String( command, Py_single_input, mainDict, mainDict );
  
  if (ret == NULL) { // python command returns error
    PyErr_Print();
    return false;
  }
  
  Py_DECREF( ret );

  if (Py_FlushLine())
    PyErr_Clear();
    
  return true;
}

/**
 *  PythonSession class
 */
PythonSession::PythonSession( int id, PythonServer * server, std::string & welcomeStr ) :
  id_( id ),
  server_( server ),
  telnetSubnegotiation_( false ),
  welcomeStr_( welcomeStr ),
  promptStr_( ">>> " ),
  historyPos_( -1 ),
  charPos_( 0 ),
  multiline_( "" )
{
  //DEBUG_MSG( "PythonSesseion::PythonSession()\n" );

  sessionState_ = SESSION_CLOSED;

  // 
  // Allocate send buffer
  //
  antEnvCreateSharedBufferMsg sendBufferMsg( PS_SEND_BUFFER_SIZE );

  sendBufferMsg.Call( server_->ipstackRef_, sizeof( sendBufferMsg ) );
  if (sendBufferMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s[%d] antError %d", "PythonServer::PythonSession()",
      "Can't allocate send buffer", index, sendBufferMsg.error );
    sessionState_ = SESSION_INVALID;
    return;
  }

  sendBuffer_ = sendBufferMsg.buffer;
  sendBuffer_.Map();
  sendData_ = (byte*)(sendBuffer_.GetAddress());

  //
  // Allocate receive buffer
  //
  antEnvCreateSharedBufferMsg recvBufferMsg( PS_RECEIVE_BUFFER_SIZE );

  recvBufferMsg.Call( server_->ipstackRef_, sizeof( recvBufferMsg ) );
  if (recvBufferMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s[%d] antError %d", "PythonServer::PythonSession()",
      "Can't allocate receive buffer", index, recvBufferMsg.error );
    sessionState_ = SESSION_INVALID;
    return;
  }

  recvBuffer_ = recvBufferMsg.buffer;
  recvBuffer_.Map();
  recvData_ = (byte*)(recvBuffer_.GetAddress());

  if (this->Listen())
    sessionState_ = SESSION_READY;
}

PythonSession::~PythonSession()
{
  if (sessionState_ != SESSION_CLOSED)
    this->closeSession( false );
    
  //unmap & release shared buffer
  sendBuffer_.UnMap();
  antEnvDestroySharedBufferMsg destroySendBufferMsg( sendBuffer_ );
  destroySendBufferMsg.Call( server_->ipstackRef_, sizeof(destroySendBufferMsg) );

  // UnMap and Destroy RecvBuffer
  recvBuffer_.UnMap();
  antEnvDestroySharedBufferMsg destroyRecvBufferMsg( recvBuffer_ );
  destroyRecvBufferMsg.Call( server_->ipstackRef_, sizeof(destroyRecvBufferMsg) );
}

bool PythonSession::Listen()
{
  if (sessionState_ != SESSION_CLOSED) return false;

  //
  // Create endpoint
  //
  antEnvCreateEndpointMsg tcpCreateMsg( EndpointType_TCP,
                                         PS_RECEIVE_BUFFER_SIZE);
  tcpCreateMsg.Call( server_->ipstackRef_, sizeof( tcpCreateMsg ) );
  if (tcpCreateMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s antError %d", "PythonSession::Listen()",
      "Can't create endpoint", tcpCreateMsg.error );
    return false;
  }
  endpoint_ = tcpCreateMsg.moduleRef;

  //
  // Listen
  //
  TCPEndpointListenMsg listenMsg( endpoint_,
                                 IP_ADDR_ANY, PYTHON_SERVER_PORT );

  listenMsg.continuation = (void*)id_;

  listenMsg.Send( server_->ipstackRef_, server_->myOID_,
                 Extra_Entry[entryListenCont], sizeof( listenMsg ) );

  return true;
}

void PythonSession::Close()
{
  if (sessionState_ == SESSION_CLOSED) return;

  TCPEndpointCloseMsg closeMsg( endpoint_ );
  closeMsg.continuation = (void*)id_;

  closeMsg.Send( server_->ipstackRef_, server_->myOID_,
                Extra_Entry[entryCloseCont], sizeof( closeMsg ));

  sessionState_ = SESSION_CLOSED;
}

void PythonSession::Send( const char * str )
{
  if (strlen( str ) == 0) return;

  //DEBUG_MSG(("PythonSession::Send() send over %s chars\n", str));

  TCPEndpointSendMsg sendMsg( endpoint_, sendData_, strlen( (char *)sendData_ ) );
  sendMsg.continuation = (void*)id_;

  sendMsg.Send( server_->ipstackRef_, server_->myOID_,
               Extra_Entry[entrySendCont], sizeof( sendMsg ) );
}

void PythonSession::Receive()
{
  // Setup receiving mesg
  if (sessionState_ != SESSION_RECEIVING) {
    DEBUG_MSG( "PythonSession::Receive: PythonSession not in receiving state\n" );
    return;
  }

  TCPEndpointReceiveMsg receiveMsg( endpoint_, recvData_, 1, PYTHONSERVER_BUFFER_SIZE );
  receiveMsg.continuation = (void*)id_;

  receiveMsg.Send( server_->ipstackRef_, server_->myOID_,
                  Extra_Entry[entryReceiveCont], sizeof( receiveMsg ) );
}

void PythonSession::connectReady()
{
  unsigned char options[] =
  {
    TELNET_IAC, TELNET_WILL, TELNET_ECHO,
    TELNET_IAC, TELNET_WONT, TELNET_LINEMODE, 0
  };

  this->expectInput( false );
  this->write( (char*)options );
  this->write( welcomeStr_.c_str() );
  this->write( "\r\n" );
  this->writePrompt();
  this->expectInput( true );
}

inline void PythonSession::expectInput( bool flag )
{
#ifdef DEBUG
  switch (sessionState_) {
    case SESSION_INVALID:
    case SESSION_CLOSED:
    case SESSION_READY:
      DEBUG_MSG( "Python session is in a non-communicating state\n" );
      return;
      break;
    case SESSION_CONNECTED:
      DEBUG_MSG( "Python session is in connected state\n" );
      break;
  }
#endif
  if (flag) {
    sessionState_ = SESSION_RECEIVING;
    this->Receive();
  }
  else {
    sessionState_ = SESSION_SENDING;
  }
}

int PythonSession::handleInput( int byteReceived )
{
  if (!byteReceived) {
    DEBUG_MSG( "PythonSession::handleInput: receive NULL data\n" );
    return 0;
  }

  this->expectInput( false );

  for(int i = 0; i < byteReceived; i++) {
    readBuffer_.push_back( (char)recvData_[i] );
  }

  while (!readBuffer_.empty()) {
    int c = (unsigned char)readBuffer_[0];

    // Handle (and ignore) telnet protocol commands.

    if (c == TELNET_IAC) {
      if (!this->handleTelnetCommand())
        return 1;
      continue;
    }

    if (c == KEY_ESC) {
      if (!this->handleVTCommand())
        return 1;
      continue;
    }

    // If we're in telnet subnegotiation mode, ignore normal chars.

    if (telnetSubnegotiation_) {
      readBuffer_.pop_front();
      continue;
    }

    // If we got something printable, echo it and append it to
    // the current line.

    if (isprint( c )) {
      this->handleChar();
      continue;
    }

    switch (c) {
      case KEY_ENTER:
        this->handleLine();
        break;

      case KEY_BACKSPACE:
      case KEY_DEL:
        this->handleDel();
        break;

      case KEY_HTAB:
        this->handleTab();
      break;

      case KEY_CTRL_C:
      case KEY_CTRL_D:
        this->closeSession();
        return 1;

      default:
        readBuffer_.pop_front();
        break;
    }
  }

  this->expectInput( true );
  return 1;
}

void PythonSession::closeSession( bool newConn )
{
  this->expectInput( false );
  readBuffer_.clear();
  historyBuffer_.clear();
  currentLine_ = "";
  multiline_ = "";
  charPos_ = 0;
  this->write( "\r\nGoodbye from AIBO Python server\r\n" );
  this->Close();
  if (newConn)
    this->Listen();
}

bool PythonSession::handleTelnetCommand()
{
  unsigned int cmd = (unsigned char)readBuffer_[1];
  unsigned int bytesNeeded = 2;
  char str[256];

  switch (cmd) {
    case TELNET_WILL:
    case TELNET_WONT:
    case TELNET_DO:
    case TELNET_DONT:
      bytesNeeded = 3;
      break;

    case TELNET_SE:
      telnetSubnegotiation_ = false;
      break;

    case TELNET_SB:
      telnetSubnegotiation_ = true;
      break;

    case TELNET_IAC:
      // A literal 0xff. We don't care!
      break;

    default:
      sprintf( str, "Telnet command %d is unsupported.\r\n",
        cmd );
      this->write( str );
      break;
  }

  if (readBuffer_.size() < bytesNeeded)
    return false;

  while (bytesNeeded) {
    bytesNeeded--;
    readBuffer_.pop_front();
  }

  return true;
}

bool PythonSession::handleVTCommand()
{
  // Need 3 chars before we are ready.
  if (readBuffer_.size() < 3)
    return false;

  // Eat the ESC.
  readBuffer_.pop_front();

  if (readBuffer_.front() != '[' && readBuffer_.front() != 'O')
    return true;

  // Eat the [
  readBuffer_.pop_front();

  switch (readBuffer_.front()) {
    case 'A':
      this->handleUp();
      break;

    case 'B':
      this->handleDown();
      break;

    case 'C':
      this->handleRight();
      break;

    case 'D':
      this->handleLeft();
      break;
    default:
      return true;
  }

  readBuffer_.pop_front();
  return true;
}


/**
 *   This method handles a single character. It appends or inserts it
 *   into the buffer at the current position.
 */
void PythonSession::handleChar()
{
  currentLine_.insert( charPos_, 1, (char)readBuffer_.front() );
  int len = currentLine_.length() - charPos_;
  this->write( currentLine_.substr(charPos_, len).c_str() );

  char * bstr = new char[len];
  for(int i = 0; i < len - 1; i++)
    bstr[i] = '\b';
  bstr[len-1] = '\0';

  this->write( bstr );
  delete [] bstr;

  charPos_++;
  readBuffer_.pop_front();
}

/**
 *   This method handles an end of line. It executes the current command,
 *  and adds it to the history buffer.
 */
void PythonSession::handleLine()
{
  readBuffer_.pop_front();
  this->write( "\r\n" );

  if (currentLine_.empty()) {
    currentLine_ = multiline_;
    multiline_ = "";
  }
  else {
    historyBuffer_.push_back( currentLine_ );

    if (historyBuffer_.size() > MAX_HISTORY_COMMAND) {
      historyBuffer_.pop_front();
    }

    if (!multiline_.empty()) {
      multiline_ += "\n" + currentLine_;
      currentLine_ = "";
    }
  }

  if (!currentLine_.empty()) {
    currentLine_ += "\n";

    if (currentLine_[ currentLine_.length() - 2 ] == ':') {
      multiline_ += currentLine_;
    }
    else {
      server_->directOutputTo( id_ );
      server_->RunMyString(( char *)currentLine_.c_str() );
      server_->directOutputTo( -1 ); // reset output
    }
  }

  currentLine_ = "";
  historyPos_ = -1;
  charPos_ = 0;

  this->writePrompt();
}


/**
 *  This method handles a del character.
 */
void PythonSession::handleDel()
{
  if (charPos_ > 0) {
    currentLine_.erase( charPos_ - 1, 1 );
    this->write( "\b" ERASE_EOL );
    charPos_--;
    int len = currentLine_.length() - charPos_;
    this->write( currentLine_.substr(charPos_, len).c_str() );

    char * bstr = new char[len+1];
    for(int i = 0; i < len; i++)
      bstr[i] = '\b';

    bstr[len] = '\0';
    this->write( bstr );
    delete [] bstr;
  }

  readBuffer_.pop_front();
}

/**
 *  This method handles a TAB character.
 */
void PythonSession::handleTab()
{
  // insert tab char
  readBuffer_[0] = (unsigned char) '\t';
  this->handleChar();
}

/**
 *   This method handles a key up event.
 */
void PythonSession::handleUp()
{
  if (historyPos_ < (int)historyBuffer_.size() - 1) {
    historyPos_++;
    currentLine_ = historyBuffer_[historyBuffer_.size() -
      historyPos_ - 1];

    this->write( "\r" ERASE_EOL );
    this->writePrompt();
    this->write( currentLine_.c_str() );
    charPos_ = currentLine_.length();
  }
}


/**
 *   This method handles a key down event.
 */
void PythonSession::handleDown()
{
  if (historyPos_ >= 0 ) {
    historyPos_--;

    if (historyPos_ == -1) {
      currentLine_ = "";
    }
    else {
      currentLine_ = historyBuffer_[historyBuffer_.size() -
        historyPos_ - 1];
    }

    this->write( "\r" ERASE_EOL );
    this->writePrompt();
    this->write( currentLine_.c_str() );
    charPos_ = currentLine_.length();
  }
}


/**
 *   This method handles a key left event.
 */
void PythonSession::handleLeft()
{
  if (charPos_ > 0) {
    charPos_--;
    this->write( "\033[D" );
  }
}


/**
 *   This method handles a key left event.
 */
void PythonSession::handleRight()
{
  if (charPos_ < currentLine_.length()) {
    charPos_++;
    this->write( "\033[C" );
  }
}


/**
 *  This method sends output to the TCP socket.
 */
void PythonSession::write( const char* str )
{
  this->Send( str );
}

/**
 *   This method prints a prompt to the socket.
 */
void PythonSession::writePrompt()
{
  this->write( multiline_.empty() ? ">>> " : "... " );
}

void PythonServer::initPyConnect()
{
  pPyConnectStub_ = PyConnectStub::init( this );
  pPyConnect_ = pPyConnectStub_->getPyConnect();
  Py_INCREF( pPyConnect_ );
  RunMyString( "import PyConnect" );
}

void PythonServer::write( const char * msg )
{
  if (activeSession_ < PYTHON_SESSION_MAX &&
    activeSession_ != -1 )
  {
    sessions_[activeSession_]->Send( msg );
  }
  else { // send to debugging port
    INFO_MSG( "Script: %s\n", msg );
  }
}
