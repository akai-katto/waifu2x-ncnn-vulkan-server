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


#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <vector>
#pragma comment (lib, "Ws2_32.lib")

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
    static void dummy_write(void *context, void *data, int len) {
        memcpy(Writer::byte_array + offset, data, len);
        Writer::offset += len;
    }
};
char *Writer::byte_array = new char[500000000];
int Writer::offset = 0;
RawImage Writer::rawimage;
ncnn::Mat Writer::outimage;

class ImageUpscalerSender{

public:

    // only call this once
    static void setup(const char* port){

        ImageUpscalerSender::port = port;
        // Set up network protocol
        WSADATA wsaData;
        struct addrinfo hints;
        struct addrinfo *server = NULL;

        //Initialize Winsock
        std::cout << port << " " << "Intializing Winsock..." << std::endl;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        //Setup hints
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        //Setup Server
        std::cout << port << " " << "Setting up server..." << std::endl;
        getaddrinfo(static_cast<LPCTSTR>(IP_ADDRESS), port, &hints, &server);

        //Create a listening socket for connecting to server
        std::cout << port << " " << "Creating server socket..." << std::endl;
        server_socket = socket(server->ai_family, server->ai_socktype, server->ai_protocol);

        //Setup socket options
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &OPTION_VALUE,
                   sizeof(int)); //Make it possible to re-bind to a port that was used within the last 2 minutes
        setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, &OPTION_VALUE, sizeof(int)); //Used for interactive programs
        //Assign an address to the server socket.
        std::cout << port << " " << "Binding socket..." << std::endl;
        int result = bind(server_socket, server->ai_addr, (int) server->ai_addrlen);
        cout << port << " " << "bind result: " << result << endl;

        //Listen for incoming connections.
        std::cout << port << " " << "Listening..." << std::endl;
        listen(server_socket, SOMAXCONN);

        waifu2x = new Waifu2x(0, 0, 4);
    }

    ImageUpscalerSender(){
        cout << this->port << " " << "Waiting for incoming_socket acceptance for sender" << endl;
        this->incoming_socket = accept(server_socket, NULL, NULL);
        if (this->incoming_socket == INVALID_SOCKET) {
            cout << this->port << " " << "INVALID BIND ERROR" << endl;
            // todo
            //return -1;
        }
        cout << this->port << " " << "accepted!" << endl;
    }

    ~ImageUpscalerSender(){
        closesocket(this->incoming_socket);
    }

    void receive_hyperparameters(){
        char tempmsg[METADATA_MSG_SIZE];
        memset(tempmsg, 0, METADATA_MSG_SIZE);

        recv(this->incoming_socket, tempmsg, METADATA_MSG_SIZE - 1, 0);

        cout << "temp message" << endl;
        cout << tempmsg << endl;
        Document d;
        d.Parse(tempmsg);

        cout << "noise is " << d["noise"].GetInt() << endl;

        string param_path =d["param_path"].GetString();
        string model_path =d["model_path"].GetString();

        if (current_param_path != param_path && current_model_path != model_path){
            //waifu2x->~Waifu2x();
            free(waifu2x);

            int tta_mode = 0;
            waifu2x = new Waifu2x(d["gpuid"].GetInt(), d["tta"].GetInt(), 4);
            cout << "model + param is different, changing" << endl;
            waifu2x->load(param_path,model_path);
            current_param_path = param_path;
            current_model_path = model_path;
        }


        waifu2x->noise = d["noise"].GetInt();
        waifu2x->scale = d["scale"].GetInt();
        waifu2x->tilesize = d["tilesize"].GetInt();
        waifu2x->prepadding =  d["prepadding"].GetInt();
    }

    void send_upscaled_image(){

        char tempmsg[1];
        cout << this->port << " " << "sending upscaled image" << endl;
        // Reset Writer
        Writer::offset = 0;
        memset(Writer::byte_array, 0, 500000000);

        auto start = high_resolution_clock::now();
        stbi_write_bmp_to_func(Writer::dummy_write, nullptr, Writer::outimage.w, Writer::outimage.h, 3, Writer::outimage.data);

        string length = std::to_string(Writer::offset);
        padTo(length,20); // Pad Message for Python to be able to correctly interpret length

        recv(this->incoming_socket, tempmsg, 1, 0);
        send(this->incoming_socket, length.c_str(), 20, 0);
        recv(this->incoming_socket, tempmsg, 1, 0);
        send(this->incoming_socket, Writer::byte_array, 500000000, 0);
        auto stop = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(stop - start);
        cout << this->port << " " << "send duration " << duration.count() << endl;
    }

    void upscale_image(){
        int n = 3;
        ncnn::Mat inimage = ncnn::Mat(Writer::rawimage.width, Writer::rawimage.height, (void *) Writer::rawimage.data, (size_t) n, n);
        Writer::outimage = ncnn::Mat(Writer::rawimage.width * 2, Writer::rawimage.height * 2, (size_t) n, (int) n);
        this->waifu2x->process(inimage, Writer::outimage);
    }
private:
    inline static const char* port;
    SOCKET incoming_socket = INVALID_SOCKET;
    inline static SOCKET server_socket = INVALID_SOCKET;
    inline static Waifu2x* waifu2x;

    inline static string current_param_path = "";
    inline static string current_model_path = "";
};



class ImageUpscalerReceiver{

public:

    // only call this once
    static void setup(const char* port){
        ImageUpscalerReceiver::port = port;
        // Set up network protocol
        WSADATA wsaData;
        struct addrinfo hints;
        struct addrinfo *server = NULL;

        //Initialize Winsock
        std::cout << port << " " << "Intializing Winsock..." << std::endl;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        //Setup hints
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        //Setup Server
        std::cout << port << " " << "Setting up server..." << std::endl;
        getaddrinfo(static_cast<LPCTSTR>(IP_ADDRESS), port, &hints, &server);

        //Create a listening socket for connecting to server
        std::cout << port << " " << "Creating server socket..." << std::endl;
        server_socket = socket(server->ai_family, server->ai_socktype, server->ai_protocol);

        //Setup socket options
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &OPTION_VALUE,
                   sizeof(int)); //Make it possible to re-bind to a port that was used within the last 2 minutes
        setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, &OPTION_VALUE, sizeof(int)); //Used for interactive programs
        //Assign an address to the server socket.
        std::cout << port << " " << "Binding socket..." << std::endl;
        int result = bind(server_socket, server->ai_addr, (int) server->ai_addrlen);
        cout << port << " " << "bind result: " << result << endl;

        //Listen for incoming connections.
        std::cout << port << " " << "Listening..." << std::endl;
        listen(server_socket, SOMAXCONN);
    }

    ImageUpscalerReceiver(){
        cout << this->port << " " << "Waiting for incoming_socket acceptance for receiver" << endl;
        this->incoming_socket = accept(server_socket, NULL, NULL);
        if (this->incoming_socket == INVALID_SOCKET) {
            cout << this->port << " " << "INVALID BIND ERROR" << endl;
            // todo
            //return -1;
        }
        cout << this->port << " " << "accepted!" << endl;
    }

    ~ImageUpscalerReceiver(){
        closesocket(this->incoming_socket);
        stbi_image_free(Writer::Writer::rawimage.data);
    }

    // Handle Network Protocol Socket Request
    int receive_image(){

        char *buffered_in_file = new char[THIRTY_TWO_MB]; // for stb_image loading

        char tempmsg[METADATA_MSG_SIZE]; // for socket read

        cout << this->port << " " << "hi! im inside the method"  <<  endl;
        // Height
        memset(tempmsg, 0, METADATA_MSG_SIZE);
        recv(this->incoming_socket, tempmsg, METADATA_MSG_SIZE - 1, 0);
        send(this->incoming_socket, "a", 1, 0);

        cout << tempmsg << endl;
        if (!strcmp(tempmsg, "exit ")){
            cout << "exiting by request" << endl;
            return IMG_EXIT;
        }

        Writer::rawimage.width = stoi(tempmsg);
        cout << this->port << " " << "hi! im the second part "  << Writer::rawimage.width <<  endl;

        // Width
        memset(tempmsg, 0, METADATA_MSG_SIZE);
        recv(this->incoming_socket, tempmsg, METADATA_MSG_SIZE - 1, 0);
        send(this->incoming_socket, "b", 1, 0);
        Writer::rawimage.height = stoi(tempmsg);
        cout << this->port << " " << "hi! im the third part "  << Writer::rawimage.height <<  endl;

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
            std::cout << this->port << " " << "warning it failed exiting" << std::endl;
            Writer::rawimage.success_code = -1;
            free(buffered_in_file);
            return IMG_ERR;
        }

        auto stop = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(stop - start);
        cout << this->port << " " << "receive duration: " <<  duration.count() << endl;

        //stbi_write_png("onebigbuffer.png", Writer::rawimage.width, Writer::rawimage.height, 3, Writer::rawimage.data, 0);

        cout << this->port << " " << "exiting loop" << endl;
        Writer::rawimage.success_code = 0;
        free(buffered_in_file);
        return IMG_SUCCESS;
    }
private:
    inline static const char* port;
    SOCKET incoming_socket = INVALID_SOCKET;
    inline static SOCKET server_socket = INVALID_SOCKET;
};



int main(int argc, char **argv)
{
    ImageUpscalerReceiver::setup(argv[1]);
    ImageUpscalerSender::setup(argv[2]);
    cout << " " << "selected w2x vulkan port is " << argv[1] << " "  << argv[2] << endl;

    while(true) {
        ImageUpscalerReceiver comm1 = ImageUpscalerReceiver();
        int status = comm1.receive_image();

        if (status == IMG_EXIT){ return 0; }

        if (status == IMG_ERR){ return 1; }

        ImageUpscalerSender comm2 = ImageUpscalerSender();
        comm2.receive_hyperparameters();
        comm2.upscale_image();
        comm2.send_upscaled_image();
    }

    return 0;
}
