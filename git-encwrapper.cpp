/**
 * @file
 * @brief gitの入出力の文字エンコードを調整するラッパーです。
 * @author AKIYAMA Kouhei
 * @since 2010-04-23
 */

#include <vector>
#include <algorithm>
#include <string>
#include <iostream>
#include <istream>
#include <ostream>
#include <fstream>
#include <streambuf>
#include <cstdio>
#include <cerrno>
#include <cassert>
#include <iconv.h>
#include <unistd.h>
#include <sys/wait.h>
#include <boost/xpressive/xpressive.hpp>
#include <boost/lambda/lambda.hpp>
extern "C"
{
#include "libgit/attr.h"
}

// ---------------------------------------------------------------------------

static const char * const terminal_encoding = "utf-8"; ///@todo gui.encoding設定からとるべき？
//static const char * const terminal_encoding = "cp932";
static const char * const default_file_encoding = "cp932"; ///@todo システムのロケールから求めるべき？
static const char * const git_filename = "git";


// ---------------------------------------------------------------------------
// codeset converter

static const iconv_t INVALID_ICD = (iconv_t)-1;

/**
 * iconvのラッパーです。
 */
class CodesetConverter
{
    iconv_t icd;

    char *src_ptr;
    std::size_t src_bytes;
    
    static const int DST_BUF_SIZE = 1024;
    std::vector<char> dst_buf;
    char *dst_ptr;
    std::size_t dst_bytes;

public:
    CodesetConverter()
            : icd(INVALID_ICD)
            , src_ptr(NULL), src_bytes(0)
            , dst_buf(DST_BUF_SIZE)
            , dst_ptr(&dst_buf[0]), dst_bytes(DST_BUF_SIZE)
    {}

    ~CodesetConverter()
    {
        release();
    }

    void release(void)
    {
        if(icd != INVALID_ICD){
            iconv_close(icd);
            icd = INVALID_ICD;
        }
    }

    void set_encoding_to_from(const char *tocode, const char *fromcode)
    {
        release();
        icd = iconv_open(tocode, fromcode);
    }

    void convert(std::string &src_line)
    {
        dst_ptr = &dst_buf[0];
        dst_bytes = dst_buf.size();
		
        if(src_line.empty()){
            return;
        }
        src_ptr = const_cast<char *>(&src_line[0]);
        src_bytes = src_line.size();
		
        if(icd == INVALID_ICD){
			copy_src_to_dst(src_bytes);
            return;
        }
        iconv(icd, NULL, NULL, NULL, NULL);
    
        while(src_bytes > 0){
            
            if(iconv(icd, &src_ptr, &src_bytes, &dst_ptr, &dst_bytes) == std::size_t(-1)){
                switch(errno){
                default:
                case EILSEQ:
                    // invalid multibyte string found.
                    iconv(icd, NULL, NULL, NULL, NULL);
                    copy_src_to_dst(1);
                    break;
                case EINVAL:
                    // incomplete multibyte string found.
                    iconv(icd, NULL, NULL, NULL, NULL);
                    copy_src_to_dst(src_bytes);
                    break;
                case E2BIG:
                    // too short of dst_buff.
                    expand_dst_buf();
                    break;
                }
            }
        }
		return;
    }

	const char *result_begin(void) const{return &dst_buf[0];}
	const char *result_end(void) const{return dst_ptr;}
	std::size_t result_bytes(void) const{return dst_ptr - &dst_buf[0];}
	std::string result_str(void) const{return std::string(result_begin(), result_end());}

	std::string convert_str(std::string &src_line)
	{
		convert(src_line);
		return result_str();
	}

private:
    void expand_dst_buf(void)
    {
        const std::size_t pos = dst_ptr - &dst_buf[0];
        dst_buf.resize(dst_buf.size() + DST_BUF_SIZE);
        dst_bytes += DST_BUF_SIZE;
        dst_ptr = &dst_buf[0] + pos;
    }

    void copy_src_to_dst(std::size_t count)
    {
        for(; count; --count){
            if(dst_bytes == 0){
                expand_dst_buf();
            }
            *dst_ptr++ = *src_ptr++; 
            --src_bytes;
            --dst_bytes;
        }
    }
	
    CodesetConverter(const CodesetConverter &);
    const CodesetConverter &operator=(const CodesetConverter &);
};






// ---------------------------------------------------------------------------
// pipe utility

class PipeHandle
{
	int fd;
	bool valid;
public:
	explicit PipeHandle(int fd_) : valid(true), fd(fd_){}
	~PipeHandle(){ reset();}
	void reset(void)
	{
		if(valid){
			close(fd);
			valid = false;
		}
	}
	int detach(void)
	{
		assert(valid);
		if(valid){
			valid = false;
			return fd;
		}
		else{
			exit(-1);
			return 0; //? -1?
		}
	}

	operator int() const { return fd;}
private:
	PipeHandle(const PipeHandle &);
	const PipeHandle &operator=(const PipeHandle &);
};

bool create_process_with_pipe2(const char *cmd_filename, char * const cmd_argv[], int *result_pipe_rw, pid_t *result_cpid)
{
	int to_child_fd[2];
	if(pipe(to_child_fd) == -1){
		return false;
	}
	PipeHandle to_child_r(to_child_fd[0]);
	PipeHandle to_child_w(to_child_fd[1]);

	int to_parent_fd[2];
	if(pipe(to_parent_fd) == -1){
		return false;
	}
	PipeHandle to_parent_r(to_parent_fd[0]);
	PipeHandle to_parent_w(to_parent_fd[1]);

	pid_t cpid = fork();
	if(cpid == -1){
		return false;
	}

	if(cpid == 0){
		// child
		to_child_w.reset();
		to_parent_r.reset();

		close(STDOUT_FILENO);
		close(STDIN_FILENO);

		if(dup2(to_child_r, STDIN_FILENO) == -1){
			_exit(-1);
		}
		if(dup2(to_parent_w, STDOUT_FILENO) == -1){
			_exit(-1);
		}
		to_child_r.reset();
		to_parent_w.reset();

		execvp(cmd_filename, cmd_argv);
		_exit(-1);
		return false;
	}
	else{
		// parent
		to_child_r.reset();
		to_parent_w.reset();

		if(result_pipe_rw){
			result_pipe_rw[0] = to_parent_r.detach();
			result_pipe_rw[1] = to_child_w.detach();
		}
		if(result_cpid){
			*result_cpid = cpid;
		}
		return true;
	}
}

bool create_process_with_pipe_w(const char *cmd_filename, char * const cmd_argv[], int *result_pipe, pid_t *result_cpid)
{
	int to_child_fd[2];
	if(pipe(to_child_fd) == -1){
		return false;
	}
	PipeHandle to_child_r(to_child_fd[0]);
	PipeHandle to_child_w(to_child_fd[1]);

	pid_t cpid = fork();
	if(cpid == -1){
		return false;
	}

	if(cpid == 0){
		// child
		to_child_w.reset();
		close(STDIN_FILENO);
		if(dup2(to_child_r, STDIN_FILENO) == -1){
			_exit(-1);
		}
		to_child_r.reset();

		execvp(cmd_filename, cmd_argv);
		_exit(-1);
		return false;
	}
	else{
		// parent
		to_child_r.reset();

		if(result_pipe){
			*result_pipe = to_child_w.detach();
		}
		if(result_cpid){
			*result_cpid = cpid;
		}
		return true;
	}
}

bool create_process_with_pipe_r(const char *cmd_filename, char * const cmd_argv[], int *result_pipe, pid_t *result_cpid)
{
	int to_parent_fd[2];
	if(pipe(to_parent_fd) == -1){
		return false;
	}
	PipeHandle to_parent_r(to_parent_fd[0]);
	PipeHandle to_parent_w(to_parent_fd[1]);

	pid_t cpid = fork();
	if(cpid == -1){
		return false;
	}

	if(cpid == 0){
		// child
		to_parent_r.reset();

		close(STDOUT_FILENO);

		if(dup2(to_parent_w, STDOUT_FILENO) == -1){
			_exit(-1);
		}
		to_parent_w.reset();

		execvp(cmd_filename, cmd_argv);
		_exit(-1);
		return false;
	}
	else{
		// parent
		to_parent_w.reset();

		if(result_pipe){
			*result_pipe = to_parent_r.detach();
		}
		if(result_cpid){
			*result_cpid = cpid;
		}
		return true;
	}
}




// ---------------------------------------------------------------------------
// streambuf for File Descripter
// [The C++ Standard Library A Tutorial and Reference 13.13]

class FDInputStreamBuffer 
    : public std::basic_streambuf<char>
{
	int fd_;
    
    enum{ BUFFER_PUTBACK = 4};
    enum{ BUFFER_INPUT = 6};
    char_type buffer_[BUFFER_PUTBACK + BUFFER_INPUT];
public:
    explicit FDInputStreamBuffer(int fd)
            : fd_(fd)
    {
        setg(buffer_ + BUFFER_PUTBACK, //書き戻し領域の始まり
             buffer_ + BUFFER_PUTBACK, //読み取りの位置
             buffer_ + BUFFER_PUTBACK); //バッファの終わり
    }
    
protected:
    virtual int_type underflow(void)
    {
        // eback() 書き戻し領域の始まり
        // gptr()  読み取り位置
        // egptr() バッファの終わり
        // eback() <= gptr() <= egptr()
        
        if(gptr() < egptr()){
            return *gptr();
        }

        const int num_putback = std::min(gptr() - eback(), std::ptrdiff_t(BUFFER_PUTBACK));
        std::memcpy(buffer_ + BUFFER_PUTBACK - num_putback,
                    gptr() - num_putback,
                    num_putback * sizeof(char_type));

		const int actual_read = read(fd_, buffer_ + BUFFER_PUTBACK, BUFFER_INPUT);
        if(actual_read <= 0){
            return traits_type::eof();
        }

        setg(buffer_ + BUFFER_PUTBACK - num_putback,
             buffer_ + BUFFER_PUTBACK,
             buffer_ + BUFFER_PUTBACK + actual_read);

        return *gptr();
    }
};

class FDOutputStreamBuffer
    : public std::basic_streambuf<char>
{
	int fd_;

    static const std::size_t BUFFER_SIZE = 10;
    char_type buffer_[BUFFER_SIZE];
public:
    explicit FDOutputStreamBuffer(int fd)
            : fd_(fd)
    {
		setp(buffer_, buffer_ + (BUFFER_SIZE - 1));
    }

	virtual ~FDOutputStreamBuffer()
	{
		sync();
	}

protected:
	int flush_buffer(void)
	{
		const int num = pptr() - pbase();
		if(write(fd_, buffer_, num) != num){
			return traits_type::eof();
		}
		pbump(-num);
		return num;
	}

    virtual int_type overflow(int_type c)
    {
        if(c != traits_type::eof()){
			*pptr() = c;
			pbump(1);
		}
		if(flush_buffer() == traits_type::eof()){
			return traits_type::eof();
        }
        return c;
    }

	virtual int sync(void)
	{
		if(flush_buffer() == traits_type::eof()){
			return -1;
		}
		return 0;
	}
};

class FDInputStream
	: public std::istream
{
	FDInputStreamBuffer buf_;
public:
	explicit FDInputStream(int fd) : buf_(fd), std::istream(&buf_){}
};

class FDOutputStream 
	: public std::ostream
{
	FDOutputStreamBuffer buf_;
public:
	explicit FDOutputStream(int fd) : buf_(fd), std::ostream(&buf_){}
};



// ---------------------------------------------------------------------------
// file encoding detection (.gitattributes)

/**
 * 指定されたファイルの文字エンコーディングを求めます。
 * .gitattributes等から求めます。
 * 特に指定が見つからなければdefault_file_encodingを返します。
 */
const char *get_file_encoding(const std::string &filename)
{
    struct git_attr *attr_encoding = git_attr("encoding");
    
    struct git_attr_check attr_check;
    attr_check.attr = attr_encoding;
    if(!git_checkattr(filename.c_str(), 1, &attr_check)){
		const char *value = attr_check.value;
		if (ATTR_TRUE(value)) {
            return default_file_encoding;
		}
        else if (ATTR_FALSE(value)) {
            return default_file_encoding;
		}
        else if (ATTR_UNSET(value)) {
            return default_file_encoding;
		}
        else {
            return value;
		}
    }
    else{
        return default_file_encoding;
    }
}





// ---------------------------------------------------------------------------

/**
 * 差分テキスト(パッチ)のファイルごとの文字コードを統一したり、ばらしたりします。
 *
 * 例:
 * 次のような入力を、
 *
 *  diff --git a/cp932file.txt b/cp932file.txt
 *  index 3a3dfa5..904ce99 100644
 *  --- a/changes.txt
 *  +++ b/changes.txt
 *  @@ -4,3 +4,4 @@
 *   これはCP932で書かれている。
 *   テストテスト。
 *   あいうえお。
 *  +この行を追加。
 *  diff --git a/utf8file.txt b/utf8file.txt
 *  index 1b6f11d..189cd07 100644
 *  --- a/utf8file.txt
 *  +++ b/utf8file.txt
 *  @@ -6,3 +6,4 @@
 *   (文字化けしている)
 *   (文字化けしている)
 *   (文字化けしている)
 *  +(文字化けしている)
 *
 * 次のように変換したり、その逆をしたりします。
 *
 *  diff --git a/cp932file.txt b/cp932file.txt
 *  index 3a3dfa5..904ce99 100644
 *  --- a/changes.txt
 *  +++ b/changes.txt
 *  @@ -4,3 +4,4 @@
 *   これはCP932で書かれている。
 *   テストテスト。
 *   あいうえお。
 *  +この行を追加。
 *  diff --git a/utf8file.txt b/utf8file.txt
 *  index 1b6f11d..189cd07 100644
 *  --- a/utf8file.txt
 *  +++ b/utf8file.txt
 *  @@ -6,3 +6,4 @@
 *   これはUTF-8で書かれている。
 *   テストテスト。
 *   あいうえお。
 *  +この行を追加。
 *
 */
void filter_patch(std::ostream &dst_stream, std::istream &src_stream, bool src_is_terminal, bool no_prefix)
{
	using namespace boost::xpressive;
	CodesetConverter cvt;
	const sregex re_diff_header = sregex::compile(
		no_prefix ? "^diff --git ([^ ]+) ([^ ]+)$" : "^diff --git a/([^ ]+) b/([^ ]+)$"); ///@todo --src-prefix(diff),--dst-prefix(diff), -p1(apply)
	
	std::string src_line;
	while(std::getline(src_stream, src_line)){

		smatch m;
		if(regex_match(src_line, m, re_diff_header)){
			const char * const file_encoding = get_file_encoding(m[1]);
#if 0
			std::cout << "file:" << m[1] << std::endl;
			std::cout << "encoding:" << file_encoding << std::endl;
#endif
			
			if(src_is_terminal){
				cvt.set_encoding_to_from(file_encoding, terminal_encoding);
			}
			else{
				cvt.set_encoding_to_from(terminal_encoding, file_encoding);
			}
		}

		dst_stream << cvt.convert_str(src_line);
		if(!src_stream.eof()){ //行がEOFで終わっていないなら改行する。
			dst_stream << '\n';
		}
	}
	dst_stream.flush();
}

int filter_patch_git_to_wrapper(char **argv, bool no_prefix)
{
	int child_pipe_int;
	pid_t child_pid;
	if(!create_process_with_pipe_r(git_filename, argv, &child_pipe_int, &child_pid)){
		return -1;
	}
	PipeHandle child_pipe(child_pipe_int);
	FDInputStream child_stream(child_pipe);

	filter_patch(std::cout, child_stream, false, no_prefix);
	int status;
	waitpid(child_pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int filter_patch_wrapper_to_git(char **argv, bool no_prefix)
{
	int child_pipe_int;
	pid_t child_pid;
	if(!create_process_with_pipe_w(git_filename, argv, &child_pipe_int, &child_pid)){
		return -1;
	}
	PipeHandle child_pipe(child_pipe_int);
	FDOutputStream child_stream(child_pipe);

	filter_patch(child_stream, std::cin, true, no_prefix);
	child_pipe.reset(); //EOF
	int status;
	waitpid(child_pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}


const char *parse_git_command_name(int argc, char *argv[], bool &no_prefix)
{
	const char *cmdname = "";
	int i = 1;
	while(i < argc){
		const char *arg = argv[i++];
		if(arg[0] == '-'){ // skip options
			if(arg[1] == 'c' && i < argc){ //-c name=value
				++i; // skip "name=value"
			}
		}
		else{
			cmdname = arg;
			break;
		}
	}
	const bool cmd_is_diff = strcmp(cmdname, "diff") == 0;
	const bool cmd_is_apply = strcmp(cmdname, "apply") == 0;
	while(i < argc){
		const char *arg = argv[i++];
		if(arg[0] == '-'){
			if(arg[1] == '-'){
				if(arg[2] == '\0'){
					break;
				}
				else if(cmd_is_diff && strcmp(arg, "--no-prefix") == 0){
					no_prefix = true;
				}
			}
			else if(cmd_is_apply && strcmp(arg, "-p0") == 0){
				no_prefix = true;
				///@todo Support -p<n> (n>=2)
			}
			///@todo --src-prefix=a/
			///@todo --dst-prefix=b/
		}
		else{
			break;
		}
	}
	return cmdname;
}



int main(int argc, char*argv[])
{
	// argv[0]をgit_filenameにしないと、
	// 「fatal: cannot handle encwrapper internally」
	// のようなエラーが発生する場合があるようなので、argv[0]を書き換える。
	argv[0] = const_cast<char *>(git_filename);

	// gitコマンド名を取得する。ハイフン以外で始まる最初の引数。
	using namespace boost::lambda;
	bool no_prefix = false;
	const char * const cmdname = parse_git_command_name(argc, argv, no_prefix);

#if 0
	std::ofstream fs("/tmp/git-encwrapper.log", std::ios::out | std::ios::app | std::ios::ate);
	fs << "[DEBUG]";
	for(int i = 0; i < argc; ++i){
		fs << argv[i] << " ";
	}
	fs << std::endl;
#endif

	if(strcmp(cmdname, "diff") == 0){
		return filter_patch_git_to_wrapper(argv, no_prefix);
	}
	else if(strcmp(cmdname, "apply") == 0){
		return filter_patch_wrapper_to_git(argv, no_prefix);
	}
	else{
		execvp(git_filename, argv);
		return -1;
	}
	return 0;
}
