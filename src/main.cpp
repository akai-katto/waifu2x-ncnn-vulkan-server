// waifu2x implemented with ncnn library

#include <stdio.h>
#include <algorithm>
#include <queue>
#include <vector>
#include <clocale>
#include <sys/types.h>
#include <iostream>
#include <thread>
#include "rapidjson/document.h"

using namespace std;
using namespace std::chrono;
using namespace rapidjson;

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

// ncnn
#include "cpu.h"
#include "gpu.h"
#include "platform.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "waifu2x.h"

// asio
#include <asio.hpp>
using asio::ip::tcp;

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define IP_ADDRESS "localhost"
#define DEFAULT_PORT "3504"
#define DEFAULT_BUFLEN 2000000
#define TWO_MB 1000000
#define THIRTY_TWO_MB 32000000
#define METADATA_MSG_SIZE 1000

#define IMG_SUCCESS 0
#define IMG_ERR 1
#define IMG_EXIT 2

const char OPTION_VALUE = 1;


void padTo(std::string &str, const size_t num, const char paddingChar = ' ')
{
    if(num > str.size())
        str.insert(0, num - str.size(), paddingChar);
}

struct RawImage{
    int success_code;
    int width;
    int height;
    unsigned char *data = nullptr;
};


class Writer {
public:
    static char *byte_array;
    static int offset;
    static RawImage rawimage;
    static ncnn::Mat outimage;
    static Waifu2x *waifu2x;
    static int scale;
    static string current_param_path;
    static string current_model_path;
    static void dummy_write(void *context, void *data, int len) {
        memcpy(Writer::byte_array + offset, data, len);
        Writer::offset += len;
    }
};
char *Writer::byte_array = new char[500000000];
int Writer::offset = 0;
Waifu2x *Writer::waifu2x = nullptr;
RawImage Writer::rawimage;
ncnn::Mat Writer::outimage;
string Writer::current_param_path = "";
string Writer::current_model_path = "";
int Writer::scale = 2;

class ImageUpscalerSender{

public:

    ImageUpscalerSender(int port){
        tcp::acceptor acceptor(this->io_context, tcp::endpoint(tcp::v4(), port));

        // Wait for a connection
        this->socket = new tcp::socket(this->io_context);
        acceptor.accept(*socket);
    }

    ~ImageUpscalerSender(){
        stbi_image_free(Writer::Writer::rawimage.data);

        asio::error_code error;
        this->socket->close(error);
        if (error){
            cout << "error: " << error;
        }
        free(this->socket);
    }

    void send_upscaled_image(){
        std::array<char, 4> doneack = {'d', 'o', 'n', 'e'};
        std::array<char, 4096> sendbuf;
        std::array<char, 1> readbuf;
        asio::error_code error;
        // std::cout << this->port << " " << "sending upscaled image" << std::endl;;
        // Reset Writer
        Writer::offset = 0;
        stbi_write_bmp_to_func(Writer::dummy_write, nullptr, Writer::outimage.w, Writer::outimage.h, 3, Writer::outimage.data);

        for(int i = 0; i < Writer::offset; i += 4096){
            for (int j = 0; j < 4096; j++){
                sendbuf[j] = Writer::byte_array[i + j];
            }
            asio::write(*this->socket,asio::buffer(sendbuf, sendbuf.size()));
            socket->read_some(asio::buffer(readbuf), error);
        }
        asio::write(*this->socket,asio::buffer(doneack, doneack.size()));
        socket->read_some(asio::buffer(readbuf), error);
    }

    static void upscale_image(){
        int n = 3;
        int scale_run_count = 0;

        if (Writer::scale == 2)
        {
            scale_run_count = 1;
        }
        if (Writer::scale == 4)
        {
            scale_run_count = 2;
        }
        if (Writer::scale == 8)
        {
            scale_run_count = 3;
        }
        if (Writer::scale == 16)
        {
            scale_run_count = 4;
        }
        if (Writer::scale == 32)
        {
            scale_run_count = 5;
        }

        ncnn::Mat inimage = ncnn::Mat(Writer::rawimage.width, Writer::rawimage.height, (void *) Writer::rawimage.data, (size_t) n, n);
        Writer::outimage = ncnn::Mat(Writer::rawimage.width * min(Writer::scale, 2), Writer::rawimage.height * min(Writer::scale,2), (size_t) n, (int) n);
        Writer::waifu2x->process(inimage, Writer::outimage);

        for (int i = 1; i < scale_run_count; i++)
        {
            // Magic number 2, since if we're in here, we're at least some multiple of 2 scaling.
            ncnn::Mat tmp = Writer::outimage;
            Writer::outimage = ncnn::Mat(tmp.w * 2, tmp.h * 2, (size_t) n, (int) n);
            Writer::waifu2x->process(tmp, Writer::outimage);
        }
        //cout << "done upscaling" << endl;
    }
private:
    static inline asio::io_context io_context;
    tcp::socket* socket;

    inline static string current_param_path = "";
    inline static string current_model_path = "";
};



class ImageUpscalerReceiver{

public:

    ImageUpscalerReceiver(int port){
        tcp::acceptor acceptor(this->io_context, tcp::endpoint(tcp::v4(), port));

        // Wait for a connection
        this->socket = new tcp::socket(this->io_context);
        acceptor.accept(*socket);
        this->port = port;
    }

    ~ImageUpscalerReceiver(){

        asio::error_code error;
        this->socket->close(error);
        if (error){
            cout << "error: " << error;
        }
        free(this->socket);
    }

    int receive_hyperparameters(){
        std::array<char, 1> ack = {'a'};
        std::array<char, 65536> buf;
        asio::error_code error;
        size_t len;

        len = socket->read_some(asio::buffer(buf), error);
        string tempmsg = string(buf.data(), len);
        //cout << tempmsg << endl;
        if (!strcmp(tempmsg.substr(0,4).c_str(), "exit")){
            std::cout << this->port << " exiting by request" << std::endl;;
            return IMG_EXIT;
        }

        Document d;
        d.Parse(tempmsg.c_str());

        // std::cout << "noise is " << d["noise"].GetInt() << std::endl;;

        string param_path =d["param_path"].GetString();
        string model_path =d["model_path"].GetString();

        if (Writer::current_param_path != param_path && Writer::current_model_path != model_path){
            //waifu2x->~Waifu2x();
            free(Writer::waifu2x);

            int tta_mode = 0;
            Writer::waifu2x = new Waifu2x(d["gpuid"].GetInt(), d["tta"].GetInt(), 4);
            // std::cout << "model + param is different, changing" << std::endl;;
            Writer::waifu2x->load(param_path,model_path);
            Writer::current_param_path = param_path;
            Writer::current_model_path = model_path;
        }
        //cout << "done loaded" << endl;

        Writer::scale = d["scale"].GetInt();
        Writer::waifu2x->tilesize = d["tilesize"].GetInt();
        Writer::waifu2x->prepadding =  d["prepadding"].GetInt();
        Writer::waifu2x->noise = d["noise"].GetInt();
        Writer::waifu2x->scale = std::min(d["scale"].GetInt(), 2);
        asio::write(*this->socket,asio::buffer(ack, ack.size()));
        return IMG_SUCCESS;
    }

    // Handle Network Protocol Socket Request
    int receive_image(){
        char *buffered_in_file = new char[THIRTY_TWO_MB];
        std::array<char, 1> ack = {'a'};
        std::array<char, 4096> buf;
        asio::error_code error;
        size_t len;

        string tempmsg = "";

        // std::cout << this->port << " " << "hi! im inside the method"  <<  std::endl;;
        // Width
        len = this->socket->read_some(asio::buffer(buf), error);
        asio::write(*this->socket,asio::buffer(ack, ack.size()));
        tempmsg = string(buf.data(), len);
        Writer::rawimage.width = stoi(tempmsg);
        //std::cout << " " << "hi! im the second part "  << Writer::rawimage.width <<  std::endl;;

        // height
        len = this->socket->read_some(asio::buffer(buf), error);
        asio::write(*this->socket,asio::buffer(ack, ack.size()));
        tempmsg = string(buf.data(), len);
        Writer::rawimage.height = stoi(tempmsg);
        //std::cout << " " << "hi! im the second part "  << Writer::rawimage.height <<  std::endl;;

        auto start = high_resolution_clock::now();
        memset(buffered_in_file, 0, THIRTY_TWO_MB);

        size_t offset = 0;
        while (strcmp(tempmsg.c_str(), "done")){
            // cout << "first part of loop" << endl;
            asio::write(*this->socket,asio::buffer(ack, ack.size()));
            len = this->socket->read_some(asio::buffer(buf), error);
            tempmsg = string(buf.data(), len);
            // cout << "in loop" << endl;
            for (int i = 0; i < len; i++){
                buffered_in_file[offset] = buf[i];
                offset+=1;
            }
        }

        int channels = 3;
        Writer::rawimage.data = nullptr;
        Writer::rawimage.data = stbi_load_from_memory((unsigned char *) buffered_in_file,
                                                      THIRTY_TWO_MB,
                                                      &Writer::rawimage.width,
                                                      &Writer::rawimage.height, &channels, 3);

        if (Writer::rawimage.data == nullptr) {
            // std::// std::cout << this->port << " " << "warning it failed exiting" << std::std::endl;;
            Writer::rawimage.success_code = -1;
            free(buffered_in_file);
            return IMG_ERR;
        }

        auto stop = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(stop - start);
        //std::cout << "receive duration: " <<  duration.count() << std::endl;;

        //stbi_write_png("onebigbuffer.png", Writer::rawimage.width, Writer::rawimage.height, 3, Writer::rawimage.data, 0);

        // std::cout << this->port << " " << "exiting loop" << std::endl;;
        Writer::rawimage.success_code = 0;
        free(buffered_in_file);
        return IMG_SUCCESS;
    }
private:
    static inline asio::io_context io_context;
    static inline tcp::socket* socket;
    int port;
};



int main(int argc, char **argv)
{

    while(true) {
        //cout << "next loop" << endl;
        ImageUpscalerReceiver comm1 = ImageUpscalerReceiver(atoi(argv[1]));
        int status = comm1.receive_hyperparameters();
        if (status == IMG_EXIT){ return 0; }

        status = comm1.receive_image();
        if (status == IMG_ERR){ return 1; }

        ImageUpscalerSender::upscale_image();
        ImageUpscalerSender comm2 = ImageUpscalerSender(atoi(argv[2]));
        comm2.send_upscaled_image();
    }

    return 0;
}
