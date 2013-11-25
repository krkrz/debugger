//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop

#include "CheckDebuggeeUnit.h"
#include "MainUnit.h"
#pragma package(smart_init)
//---------------------------------------------------------------------------

// ���ӁF�قȂ�X���b�h�����L���� VCL �̃��\�b�h/�֐�/�v���p�e�B��ʂ�
// ���b�h���L�̃I�u�W�F�N�g�ɑ΂��Ă� Synchronize ���g�p�ł��܂��B
//
//      Synchronize(&UpdateCaption);
//
// �Ⴆ�� UpdateCaption ���ȉ��̂悤�ɒ�`��
//
//      void __fastcall DebuggeeCheckThread::UpdateCaption()
//      {
//        Form1->Caption = "�X���b�h���珑�������܂���";
//      }
//---------------------------------------------------------------------------

__fastcall DebuggeeCheckThread::DebuggeeCheckThread(bool CreateSuspended)
	: TThread(CreateSuspended), is_first_break_(true), is_request_break_(false)
{
	OnTerminate = DebuggeeCheckThread::DebuggeeCheckThreadTerminate;
}
//---------------------------------------------------------------------------
void DebuggeeCheckThread::SetName()
{
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = "DebuggeeCheckThread";
	info.dwThreadID = -1;
	info.dwFlags = 0;

	__try
	{
		RaiseException( 0x406D1388, 0, sizeof(info)/sizeof(DWORD),(DWORD*)&info );
	}
	__except (EXCEPTION_CONTINUE_EXECUTION)
	{
	}
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::Execute()
{
	SetName();
	//---- Place thread code here ----

	command_.size_ = 0;
	command_.data_ = 0;
	debuggee_comm_area_addr_ = NULL;
	debuggee_comm_area_size_ = 0;
	debug_continue_status_ = DBG_CONTINUE;

	// �N���Ώۂ̃p�����[�^�����炤
	Synchronize(&GetParameters);

	// �f�o�b�M�N��	
	STARTUPINFO si;
	ZeroMemory(&si,sizeof(si));
	si.cb=sizeof(si);
	BOOL result;
	if( work_folder_.Length() ) {
		result = ::CreateProcess( NULL,command_line_.c_str(),NULL,NULL,FALSE,
			NORMAL_PRIORITY_CLASS|DEBUG_ONLY_THIS_PROCESS,
			NULL,
			work_folder_.c_str(),
			&si,&proc_info_);
	} else {
		result = ::CreateProcess( NULL,command_line_.c_str(),NULL,NULL,FALSE,
			NORMAL_PRIORITY_CLASS|DEBUG_ONLY_THIS_PROCESS,
			NULL,NULL,&si,&proc_info_);
	}
	if( result == 0 ) {
		// error
		ShowLastError();
	} else {
		// �v���Z�X�f�[�^��ݒ�
		Synchronize(&SetProcInfo);

#if 0
		// �N���҂�
		DWORD timeout = 50;	// 50ms�͑҂�
		while( Terminated == false ) {
			// wait for wakeup debuggee
			DWORD retwait = ::WaitForInputIdle( proc_info_.hProcess, timeout );
			if( retwait == -1 ) {
				result = 0;
				break;
			} else if( retwait == WAIT_TIMEOUT ) {
				// time out, retry
			} else {
				// �N����ʒm
				Synchronize(&WakeupDebugee);
				break;
			}
		}
		if( result == 0 ) {
			ShowLastError();
			// �v���Z�X�����I��
			::TerminateProcess(proc_info_.hProcess, 0);
		} else
#else
		Synchronize(&WakeupDebugee);
#endif
		{
			BOOL ret;
			// ���s��
			while( Terminated == false && result ) {
				DEBUG_EVENT deb_ev;
				DWORD timeout = 50;	// 50ms�͑҂�
				result = ::WaitForDebugEvent( &deb_ev, timeout );
				if( result ) {
					int breakev = HandleDebugEvent( deb_ev );
					if( breakev == 0 ) {
						::ContinueDebugEvent( proc_info_.dwProcessId, deb_ev.dwThreadId, DBG_CONTINUE );
						break;
					} else if( breakev > 0 ) {
						// �f�o�b�O�𑱍s����
						ret = ::ContinueDebugEvent( proc_info_.dwProcessId, deb_ev.dwThreadId, debug_continue_status_ );
					} else if( breakev < 0 ) {
						// �u���C�N����
						bool is_break_called = false;
						while( Terminated == false ) {
							Synchronize(&GetCommand);
							if( command_.size_ > 0 && command_.data_ && debuggee_comm_area_addr_ && (int)command_.size_ <= debuggee_comm_area_size_ ) {
								DWORD dwWrite;
								BOOL retw = ::WriteProcessMemory( proc_info_.hProcess, debuggee_comm_area_addr_,
															command_.data_, command_.size_, &dwWrite );
								delete[] command_.data_;
								command_.data_ = NULL;
								if( retw == 0 || dwWrite != command_.size_ ) {
									// �������ݎ��s
									ShowLastError();
									result = 0;
									command_.size_ = 0;
									break;
								}
								command_.size_ = 0;
								ret = ::ContinueDebugEvent( proc_info_.dwProcessId, deb_ev.dwThreadId, debug_continue_status_ );
								break;
							} else {
								if( is_break_called == false ) {
									Synchronize(&OnBreak);
									is_break_called = true;
								}
							}
							::Sleep(10);
						}
					}
				} else {
					// �^�C���A�E�g�ȊO�ŏI�������ꍇ�́A�����I�����Ă��܂��B
					DWORD lasterror = ::GetLastError();
					if( WAIT_TIMEOUT != lasterror && ERROR_SEM_TIMEOUT != lasterror ) {
						::TerminateProcess(proc_info_.hProcess, 0);
						result = 0;
						break;
					} else {
						result = 1;
					}
					// �u���[�N�v�������邩�ǂ����`�F�b�N
					Synchronize(&CheckBreakRequest);
					if( is_request_break_ ) {
						// �u���[�N�v��������
						DWORD retsus = ::SuspendThread(proc_info_.hThread);
						if( retsus == (DWORD)-1 ) {
							// �G���[�ŃT�X�y���h�ł��Ȃ�
							ShowLastError();
						} else {
							Synchronize(&GetCommand);
							if( command_.size_ > 0 && command_.data_ && debuggee_comm_area_addr_ && (int)command_.size_ <= debuggee_comm_area_size_ ) {
								DWORD dwWrite;
								BOOL retw = ::WriteProcessMemory( proc_info_.hProcess, debuggee_comm_area_addr_,
															command_.data_, command_.size_, &dwWrite );
								if( retw == 0 || dwWrite != command_.size_ ) {
									// �������ݎ��s
									ShowLastError();
								}
								command_.size_ = 0;
							}
							if( command_.data_ ) delete[] command_.data_;
							command_.data_ = NULL;
							command_.size_ = 0;
						}
						if( retsus != (DWORD)-1 ) {
							// ���W���[��
							retsus = ::ResumeThread(proc_info_.hThread);
							if( retsus == (DWORD)-1 ) {
								// ���W���[�����s�����ꍇ�́A�����I�����Ă��܂��B
								::TerminateProcess(proc_info_.hProcess, 0);
								result = 0;
								break;
                            }
						}
					}
				}
			}
		}
	}
	if( result ) {
		::CloseHandle( proc_info_.hProcess );
		::CloseHandle( proc_info_.hThread );
    }
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::GetCommand()
{
	ScriptDebuggerForm->GetFirstCommand( command_ );
	debuggee_comm_area_addr_ = ScriptDebuggerForm->GetDebugeeAreaAddr();
	debuggee_comm_area_size_ = ScriptDebuggerForm->GetDebugeeAreaSize();
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::CheckBreakRequest()
{
	is_request_break_ = ScriptDebuggerForm->IsRequestBreak;
	if( is_request_break_ ) ScriptDebuggerForm->SetBreakCommand();
}
//---------------------------------------------------------------------------
//! @return : �������p�����邩�ǂ���
//! @retval 0 : �I��
//! @retval > 0 : �p��
//! @retval < 0 : �u���[�N
int __fastcall DebuggeeCheckThread::HandleDebugEvent( DEBUG_EVENT& debug )
{
	debug_continue_status_ = DBG_CONTINUE;
	switch(debug.dwDebugEventCode){
		case OUTPUT_DEBUG_STRING_EVENT:	// �f�o�b�O���������M����
			return HandleDebugString( debug );
		case CREATE_PROCESS_DEBUG_EVENT:// �v���Z�X�𐶐�����
//			debug.u.CreateProcessInfo �ڍׂ͖��Ή�
			debug_string_ = AnsiString("�v���Z�X����������܂����B");
			Synchronize(&SetDebugString);
			break;
		case CREATE_THREAD_DEBUG_EVENT:	// �X���b�h�𐶐�����
			debug_string_ = AnsiString("�X���b�h ( 0x")
				+ AnsiString::IntToHex( debug.dwThreadId, 8 )
				+ AnsiString(" ) ���A�h���X ")
				+ AnsiString::IntToHex( (int)debug.u.CreateThread.lpStartAddress, 8 )
				+ AnsiString(" �ŊJ�n����܂����B");
			Synchronize(&SetDebugString);
			break;
		case EXIT_THREAD_DEBUG_EVENT:	// �X���b�h���I������
			debug_string_ = AnsiString("�X���b�h ( 0x")
				+ AnsiString::IntToHex( debug.dwThreadId, 8 )
				+ AnsiString(") �̓R�[�h ")
				+ AnsiString::IntToHex( debug.u.ExitThread.dwExitCode, 8 )
				+ AnsiString(" �ŏI�����܂����B");
			Synchronize(&SetDebugString);
			break;
		case LOAD_DLL_DEBUG_EVENT:		// DLL�����[�h����
			return HandleDllLoad( debug );
		case UNLOAD_DLL_DEBUG_EVENT:	// DLL���A�����[�h����
			return HandleDllUnload( debug );
		case EXCEPTION_DEBUG_EVENT:		// ��O����������
			return HandleDebugException( debug );
		case RIP_EVENT:					// RIP�C�x���g
			break;
		case EXIT_PROCESS_DEBUG_EVENT:	// �v���Z�X���I������
			debug_string_ = AnsiString("�v���O�����̓R�[�h 0x")
				+ AnsiString::IntToHex( debug.u.ExitProcess.dwExitCode, 8 )
				+ AnsiString(" �ŏI�����܂����B");
			Synchronize(&SetDebugString);
			return 0;
	}
	return 1;
}
//---------------------------------------------------------------------------
int __fastcall DebuggeeCheckThread::HandleDebugException( DEBUG_EVENT& debug )
{
	debug_continue_status_ = DBG_EXCEPTION_NOT_HANDLED;

	AnsiString theadStr( AnsiString( "�X���b�h ( " ) + AnsiString::IntToHex( debug.dwThreadId, 8 ) + AnsiString(" ) ") );
	AnsiString epiStr( AnsiString( " ( EPI = 0x" ) + AnsiString::IntToHex( (int)debug.u.Exception.ExceptionRecord.ExceptionAddress, 8 ) + AnsiString(" ) ") );

	switch( debug.u.Exception.ExceptionRecord.ExceptionCode ) {
		case EXCEPTION_ACCESS_VIOLATION:
			if( debug.u.Exception.ExceptionRecord.NumberParameters >= 2 ) {
				debug_string_ = theadStr + AnsiString("�ŁA0x");
				debug_string_ += AnsiString::IntToHex( (int)debug.u.Exception.ExceptionRecord.ExceptionInformation[1], 8 );
				debug_string_ += AnsiString("��");
				if( debug.u.Exception.ExceptionRecord.ExceptionInformation[0] ) {
					debug_string_ += AnsiString("�������ݒ��ɃA�N�Z�X�ᔽ������܂���") + epiStr;
				} else {
					debug_string_ += AnsiString("�ǂݍ��ݒ��ɃA�N�Z�X�ᔽ������܂���") + epiStr;
				}
			} else {
				debug_string_ = theadStr + AnsiString("�ŁA�A�N�Z�X�ᔽ������܂���") + epiStr;
			}
			break;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			debug_string_ = theadStr + AnsiString("�ŁA�z��͈̔͊O�ɃA�N�Z�X������܂���") + epiStr;
			break;
		case EXCEPTION_BREAKPOINT:
			debug_continue_status_ = DBG_CONTINUE;
			if( is_first_break_ ) {
				is_first_break_ = false;
			}
			else
			{
				// 1��ڂ̃u���[�N�|�C���g�̓G���g���[�|�C���g�Ŕ�������͗l
//				debug_string_ = theadStr + AnsiString("���A�u���[�N�|�C���g�Œ�~���܂���") + epiStr;
//				Synchronize(&SetDebugString);
				return ( (debug.u.Exception.ExceptionRecord.ExceptionFlags & EXCEPTION_NONCONTINUABLE) ? 0 : -1 );
			}
			break;
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			debug_string_ = theadStr + AnsiString("�ŁA�A���C�����g�G�N�Z�v�V�������������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_DENORMAL_OPERAND:
			debug_string_ = theadStr + AnsiString("�ŁA���������_���̔񐳋K�������Z���������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			debug_string_ = theadStr + AnsiString("�ŁA���������_����0���Z���������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_INEXACT_RESULT:
			debug_string_ = theadStr + AnsiString("�ŁA���������_���̉��Z���ʂ�10�i�����Ő��m�ɕ\�����邱�Ƃ̏o���Ȃ����Z���������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_INVALID_OPERATION:
			debug_string_ = theadStr + AnsiString("�ŁA���������_�����Z��O���������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_OVERFLOW:
			debug_string_ = theadStr + AnsiString("�ŁA���������_���I�[�o�[�t���[���������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_STACK_CHECK:
			debug_string_ = theadStr + AnsiString("�ŁA���������_���X�^�b�N�I�[�o�[�t���[���������܂���") + epiStr;
			break;
		case EXCEPTION_FLT_UNDERFLOW:
			debug_string_ = theadStr + AnsiString("�ŁA���������_���A���_�[�t���[���������܂���") + epiStr;
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			debug_string_ = theadStr + AnsiString("�ŁA�s���Ȗ���(invalid instruction)�̎��s���s���܂���") + epiStr;
			break;
		case EXCEPTION_IN_PAGE_ERROR:
			debug_string_ = theadStr + AnsiString("�́A���݂��Ă��Ȃ��y�[�W�ɃA�N�Z�X���悤�Ƃ��A�V�X�e���̓y�[�W�����[�h�ł��܂���ł���") + epiStr;
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			debug_string_ = theadStr + AnsiString("�ŁA0�ɂ�鏜�Z���s���܂���") + epiStr;
			break;
		case EXCEPTION_INT_OVERFLOW:
			debug_string_ = theadStr + AnsiString("�ŁA�����̃I�[�o�[�t���[���������܂���") + epiStr;
			break;
		case EXCEPTION_INVALID_DISPOSITION:
			debug_string_ = theadStr + AnsiString("�ŁA��O�n���h�����s���Ȕz�u���O�f�B�X�p�b�`���ɕԂ��܂����B������������g�p����v���O���}�͂��̗�O�Ɍ����đ������܂���") + epiStr;
			break;
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			debug_string_ = theadStr + AnsiString("�ŁA���s�s�\�ȗ�O�̌�A����Ɏ��s����܂���") + epiStr;
			break;
		case EXCEPTION_PRIV_INSTRUCTION:
			debug_string_ = theadStr + AnsiString("�́A���삪���݂̃}�V�����[�h�ŋ�����Ă��Ȃ�����(instruction)�����s���悤�Ƃ��܂���") + epiStr;
			break;
		case EXCEPTION_SINGLE_STEP:
			debug_continue_status_ = DBG_CONTINUE;
			debug_string_ = theadStr + AnsiString("�ŁA�X�e�b�v���s���s���܂���") + epiStr;
			break;
		case EXCEPTION_STACK_OVERFLOW:
			debug_string_ = theadStr + AnsiString("�ŁA�X�^�b�N�I�[�o�[�t���[���������܂���") + epiStr;
			break;
		default:
			debug_continue_status_ = DBG_CONTINUE;
			debug_string_ = theadStr + AnsiString("�ŁA�s���ȗ�O ( �R�[�h : 0x")
				+ AnsiString::IntToHex( (int)debug.u.Exception.ExceptionRecord.ExceptionCode, 8 )
				+ (" ) ���������܂���") + epiStr;

			if( debug.u.Exception.ExceptionRecord.NumberParameters ) {
				debug_string_ += AnsiString(" �ǉ���� :");
				for( int i = 0; i < debug.u.Exception.ExceptionRecord.NumberParameters; i++ ) {
					debug_string_ += AnsiString(" 0x") + AnsiString::IntToHex( (int)debug.u.Exception.ExceptionRecord.ExceptionInformation[i], 8 );
				}
			}
//			CONTEXT threadContext;
//			memset( &threadContext, 0, sizeof(threadContext) );
//			threadContext.ContextFlags = CONTEXT_ALL;
//			if( ::GetThreadContext( proc_info_.hThread, (LPCONTEXT)&threadContext ) ) {
//				
//			}
			Synchronize(&SetDebugString);
			return 1;
	}
	Synchronize(&SetDebugString);

	return ( (debug.u.Exception.ExceptionRecord.ExceptionFlags & EXCEPTION_NONCONTINUABLE) ? 0 : 1 );
}
//---------------------------------------------------------------------------
int __fastcall DebuggeeCheckThread::HandleDebugString( DEBUG_EVENT& debug )
{
	void* buffer;
	size_t len = debug.u.DebugString.nDebugStringLength;
	if( len == 0 ) return 1;
	bool isunicode = debug.u.DebugString.fUnicode ? true : false;

	if( isunicode ) {
		buffer = (void*)new wchar_t[len];
		len = len * sizeof(wchar_t);
	} else {
		buffer = (void*)new char[len];
		len = len * sizeof(char);
	}

	// �f�o�b�O�������ǂݏo��
	DWORD dwRead;
	BOOL result = ::ReadProcessMemory( proc_info_.hProcess, debug.u.DebugString.lpDebugStringData,
						buffer, len, &dwRead );
	if( result == 0 ) {
        ShowLastError();
	} else {
		if( isunicode ) {
			debug_string_ = AnsiString( (wchar_t*)buffer );
		} else {
			debug_string_ = AnsiString( (char*)buffer );
		}
		Synchronize(&SetDebugString);
	}
	delete[] buffer;
	return 1;
}
//---------------------------------------------------------------------------
int __fastcall DebuggeeCheckThread::HandleDllLoad( DEBUG_EVENT& debug )
{
	if( debug.u.LoadDll.lpImageName ) {
		// �t�@�C�����������Ă��鎞
		std::string dllname;
		wchar_t wcBuf[MAX_PATH];
		LONG_PTR lpData;
		DWORD dwAccessByte;
		::ReadProcessMemory( proc_info_.hProcess, debug.u.LoadDll.lpImageName, &lpData, sizeof(LONG_PTR), &dwAccessByte );
		::ReadProcessMemory( proc_info_.hProcess, (void *)lpData, wcBuf, sizeof(WCHAR)*MAX_PATH, &dwAccessByte );

		if( debug.u.LoadDll.fUnicode ) {
			wchar_t* name = (wchar_t*)wcBuf;
			dllname = std::string( AnsiString( name ).c_str() );
			PushDllInfo( debug.u.LoadDll.lpBaseOfDll, dllname );
		} else {
			char* name = (char*)wcBuf;
			dllname = std::string( AnsiString( name ).c_str() );
			PushDllInfo( debug.u.LoadDll.lpBaseOfDll, dllname );
		}


		debug_string_ = AnsiString( dllname.c_str() );
		debug_string_ += AnsiString( "�����[�h����܂���" );
		Synchronize(&SetDebugString);
	} else {
		// �����Ă��Ȃ��Ƃ�

	}
#if 0
	typedef struct _LOAD_DLL_DEBUG_INFO {
		HANDLE  hFile;                   /* DLL�̃t�@�C���n���h�� */
		LPVOID  lpBaseOfDll;             /* DLL�̃x�[�X�A�h���X */
		DWORD   dwDebugInfoFileOffset;   /* �f�o�b�O���܂ł̃I�t�Z�b�g */
		DWORD   nDebugInfoSize;          /* �f�o�b�O���̃T�C�Y */
		LPVOID  lpImageName;             /* DLL�̃t�@�C���� */
		WORD    fUnicode;                /* DLL�̃t�@�C�����̕����R�[�h�t���O */
	} LOAD_DLL_DEBUG_INFO;
#endif
	return 1;
}
//---------------------------------------------------------------------------
int __fastcall DebuggeeCheckThread::HandleDllUnload( DEBUG_EVENT& debug )
{
	std::string dllname;
	if( GetDllInfo( debug.u.UnloadDll.lpBaseOfDll, dllname ) ) {
		debug_string_ = AnsiString( dllname.c_str() );
		debug_string_ += AnsiString( "���A�����[�h����܂���" );
		Synchronize(&SetDebugString);
	}
	return 1;
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::PushDllInfo( LPVOID baseaddr, const std::string& filename )
{
	dll_info_.insert( std::map<LPVOID, std::string>::value_type( baseaddr, filename ) );
}
//---------------------------------------------------------------------------
bool __fastcall DebuggeeCheckThread::GetDllInfo( LPVOID baseaddr, std::string& filename )
{
	std::map<LPVOID, std::string>::const_iterator i = dll_info_.find( baseaddr );
	if( i != dll_info_.end() ) {
		filename = i->second;
		return true;
	}
	return false;
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::OnBreak()
{
	ScriptDebuggerForm->OnBreak();
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::SetDebugString()
{
	ScriptDebuggerForm->AppendDebugString( debug_string_ );
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::DebuggeeCheckThreadTerminate(TObject *Sender)
{
	ScriptDebuggerForm->TarminateDebugeeCheckThread();
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::GetParameters()
{
	command_line_ = ScriptDebuggerForm->DebuggeeCommandLine;
	work_folder_ = ScriptDebuggerForm->DebuggeeWorkingFolder;
}
//---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::WakeupDebugee()
{
	ScriptDebuggerForm->WakeupDebugee();
}
 //---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::SetProcInfo()
{
	ScriptDebuggerForm->SetProcInfo( proc_info_ );
}
 //---------------------------------------------------------------------------
void __fastcall DebuggeeCheckThread::ShowLastError()
{
	LPVOID lpMsgBuf;
	::FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER |
						FORMAT_MESSAGE_FROM_SYSTEM |
						FORMAT_MESSAGE_IGNORE_INSERTS,
						NULL, ::GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
						(LPTSTR)&lpMsgBuf, 0, NULL );
	::MessageBox( NULL, (LPCTSTR)lpMsgBuf, "Error", MB_OK | MB_ICONINFORMATION );
	LocalFree(lpMsgBuf);
}
//---------------------------------------------------------------------------
