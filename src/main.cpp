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

//class ImageUpscalerSender{
//
//public:
//
//    // only call this once
//    static void setup(const char* port){
//
//        ImageUpscalerSender::port = port;
//        // Set up network protocol
//        WSADATA wsaData;
//        struct addrinfo hints;
//        struct addrinfo *server = NULL;
//
//        //Initialize Winsock
//        // std::// std::cout << port << " " << "Intializing Winsock..." << std::std::endl;;
//        WSAStartup(MAKEWORD(2, 2), &wsaData);
//
//        //Setup hints
//        ZeroMemory(&hints, sizeof(hints));
//        hints.ai_family = AF_INET;
//        hints.ai_socktype = SOCK_STREAM;
//        hints.ai_protocol = IPPROTO_TCP;
//        hints.ai_flags = AI_PASSIVE;
//
//        //Setup Server
//        // std::// std::cout << port << " " << "Setting up server..." << std::std::endl;;
//        getaddrinfo(static_cast<LPCTSTR>(IP_ADDRESS), port, &hints, &server);
//
//        //Create a listening socket for connecting to server
//        // std::// std::cout << port << " " << "Creating server socket..." << std::std::endl;;
//        server_socket = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
//
//        //Setup socket options
//        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &OPTION_VALUE,
//                   sizeof(int)); //Make it possible to re-bind to a port that was used within the last 2 minutes
//        setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, &OPTION_VALUE, sizeof(int)); //Used for interactive programs
//        //Assign an address to the server socket.
//        // std::// std::cout << port << " " << "Binding socket..." << std::std::endl;;
//        int result = bind(server_socket, server->ai_addr, (int) server->ai_addrlen);
//        // std::cout << port << " " << "bind result: " << result << std::endl;;
//
//        //Listen for incoming connections.
//        // std::// std::cout << port << " " << "Listening..." << std::std::endl;;
//        listen(server_socket, SOMAXCONN);
//
//        Writer::waifu2x = new Waifu2x(0, 0, 0);
//    }
//
//    ImageUpscalerSender(){
//        // std::cout << this->port << " " << "Waiting for incoming_socket acceptance for sender" << std::endl;;
//        this->incoming_socket = accept(server_socket, NULL, NULL);
//        if (this->incoming_socket == INVALID_SOCKET) {
//            // std::cout << this->port << " " << "INVALID BIND ERROR" << std::endl;;
//            // todo
//            //return -1;
//        }
//        // std::cout << this->port << " " << "accepted!" << std::endl;;
//    }
//
//    ~ImageUpscalerSender(){
//        stbi_image_free(Writer::Writer::rawimage.data);
//        closesocket(this->incoming_socket);
//    }
//
//    void send_upscaled_image(){
//
//        char tempmsg[1];
//        // std::cout << this->port << " " << "sending upscaled image" << std::endl;;
//        // Reset Writer
//        Writer::offset = 0;
//        memset(Writer::byte_array, 0, 500000000);
//
//        auto start = high_resolution_clock::now();
//        stbi_write_bmp_to_func(Writer::dummy_write, nullptr, Writer::outimage.w, Writer::outimage.h, 3, Writer::outimage.data);
//
//        string length = std::to_string(Writer::offset);
//        padTo(length,20); // Pad Message for Python to be able to correctly interpret length
//
//        recv(this->incoming_socket, tempmsg, 1, 0);
//        send(this->incoming_socket, length.c_str(), 20, 0);
//        recv(this->incoming_socket, tempmsg, 1, 0);
//        send(this->incoming_socket, Writer::byte_array, 500000000, 0);
//        auto stop = high_resolution_clock::now();
//        auto duration = duration_cast<microseconds>(stop - start);
//        // std::cout << this->port << " " << "send duration " << duration.count() << std::endl;;
//    }
//
//    static void upscale_image(){
//        int n = 3;
//        int scale_run_count = 0;
//
//        if (Writer::scale == 2)
//        {
//            scale_run_count = 1;
//        }
//        if (Writer::scale == 4)
//        {
//            scale_run_count = 2;
//        }
//        if (Writer::scale == 8)
//        {
//            scale_run_count = 3;
//        }
//        if (Writer::scale == 16)
//        {
//            scale_run_count = 4;
//        }
//        if (Writer::scale == 32)
//        {
//            scale_run_count = 5;
//        }
//
//        ncnn::Mat inimage = ncnn::Mat(Writer::rawimage.width, Writer::rawimage.height, (void *) Writer::rawimage.data, (size_t) n, n);
//        Writer::outimage = ncnn::Mat(Writer::rawimage.width * min(Writer::scale, 2), Writer::rawimage.height * min(Writer::scale,2), (size_t) n, (int) n);
//        Writer::waifu2x->process(inimage, Writer::outimage);
//
//        for (int i = 1; i < scale_run_count; i++)
//        {
//            // Magic number 2, since if we're in here, we're at least some multiple of 2 scaling.
//            ncnn::Mat tmp = Writer::outimage;
//            Writer::outimage = ncnn::Mat(tmp.w * 2, tmp.h * 2, (size_t) n, (int) n);
//            Writer::waifu2x->process(tmp, Writer::outimage);
//        }
//    }
//private:
//    inline static const char* port;
//    SOCKET incoming_socket = INVALID_SOCKET;
//    inline static SOCKET server_socket = INVALID_SOCKET;
//
//    inline static string current_param_path = "";
//    inline static string current_model_path = "";
//};
//


class ImageUpscalerReceiver{

public:

    ImageUpscalerReceiver(int port){
        tcp::acceptor acceptor(this->io_context, tcp::endpoint(tcp::v4(), port));

        // Wait for a connection
        this->socket = new tcp::socket(this->io_context);
        acceptor.accept(*socket);
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

        if (!strcmp(tempmsg.c_str(), "exit")){
            std::cout << "exiting by request" << std::endl;;
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

        Writer::scale = d["scale"].GetInt();
        Writer::waifu2x->tilesize = d["tilesize"].GetInt();
        Writer::waifu2x->prepadding =  d["prepadding"].GetInt();
        Writer::waifu2x->noise = d["noise"].GetInt();
        Writer::waifu2x->scale = std::min(d["scale"].GetInt(), 2);
        asio::write(*this->socket,asio::buffer(ack, len) );
        return IMG_SUCCESS;
    }

    // Handle Network Protocol Socket Request
    int receive_image(){

        char *buffered_in_file = new char[THIRTY_TWO_MB]; // for stb_image loading

        char tempmsg[METADATA_MSG_SIZE]; // for socket read

        // std::cout << this->port << " " << "hi! im inside the method"  <<  std::endl;;
        // Height
        memset(tempmsg, 0, METADATA_MSG_SIZE);
        recv(this->incoming_socket, tempmsg, METADATA_MSG_SIZE - 1, 0);
        send(this->incoming_socket, "a", 1, 0);


        Writer::rawimage.width = stoi(tempmsg);
        // std::cout << this->port << " " << "hi! im the second part "  << Writer::rawimage.width <<  std::endl;;

        // Width
        memset(tempmsg, 0, METADATA_MSG_SIZE);
        recv(this->incoming_socket, tempmsg, METADATA_MSG_SIZE - 1, 0);
        send(this->incoming_socket, "b", 1, 0);
        Writer::rawimage.height = stoi(tempmsg);
        // std::cout << this->port << " " << "hi! im the third part "  << Writer::rawimage.height <<  std::endl;;

        auto start = high_resolution_clock::now();
        memset(buffered_in_file, 0, THIRTY_TWO_MB);
        recv(this->incoming_socket, buffered_in_file, THIRTY_TWO_MB, 0);

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
        // std::cout << this->port << " " << "receive duration: " <<  duration.count() << std::endl;;

        //stbi_write_png("onebigbuffer.png", Writer::rawimage.width, Writer::rawimage.height, 3, Writer::rawimage.data, 0);

        // std::cout << this->port << " " << "exiting loop" << std::endl;;
        Writer::rawimage.success_code = 0;
        free(buffered_in_file);
        return IMG_SUCCESS;
    }
private:
    inline static const char* port;
    asio::io_context io_context;
    tcp::socket* socket;
};



int main(int argc, char **argv)
{

    while(true) {
        cout << "next loop" << endl;
        ImageUpscalerReceiver comm1 = ImageUpscalerReceiver(3509);
        int status = comm1.receive_hyperparameters();
        if (status == IMG_EXIT){ return 0; }

//        status = comm1.receive_image();
//        if (status == IMG_ERR){ return 1; }

//        ImageUpscalerSender::upscale_image();
//        ImageUpscalerSender comm2 = ImageUpscalerSender();
//        comm2.send_upscaled_image();
    }

    return 0;
}
