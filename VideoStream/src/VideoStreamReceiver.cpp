/*
 *                         ADLINK Edge SDK
 *
 *   This software and documentation are Copyright 2018 to 2019 ADLINK
 *   Technology Limited, its affiliated companies and licensors. All rights
 *   reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

/**
 * This code is part of example scenario 1 'Connect a Sensor' of the
 * ADLINK Edge SDK. For a description of this scenario see the
 * 'Edge SDK User Guide' in the /doc directory of the Edge SDK instalation.
 *
 * For instructions on building and running the example see the README
 * file in the Edge SDK installation directory.
 */

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <future>
#include <assert.h>
#include <exception>
#include <Dispatcher.hpp>
#include <IoTDataThing.hpp>
#include <JSonThingAPI.hpp>
#include <thing_IoTData.h>
#include <ThingAPIException.hpp>
#include <inference_engine.hpp>
#include "ocv_common.hpp"
#include "slog.hpp"
#include "classification_results.h"

using namespace std;
using namespace com::adlinktech::datariver;
using namespace com::adlinktech::iot;
using namespace InferenceEngine;

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#define READ_SAMPLE_DELAY 100

cv::Mat frame;
InputsDataMap *inputInfo;
OutputsDataMap *outputInfo;
Core core;
const string *inputName;
const string *outputName;
SizeVector inputDims;
SizeVector outputDims;
size_t modelWidth;
size_t modelHeight;
size_t modelChannels;
int channelSize;
int inputSize;
int objectSize;
ExecutableNetwork execNetwork;
InferRequest::Ptr inferRequest;

int loadNetwork(string modelNetwork, string modelWeights, Core plugin, string targetDevice)
{
    // Read IR generated by ModelOptimizer and configure network
    CNNNetReader networkReader;
    networkReader.ReadNetwork(modelNetwork);
    networkReader.ReadWeights(modelWeights);
    // networkReader.getNetwork().setBatchSize(1);
    CNNNetwork network = networkReader.getNetwork();

    // Get input info
    inputInfo = new InputsDataMap(network.getInputsInfo());
    inputName = &(inputInfo->begin()->first);
    inputDims = inputInfo->begin()->second->getTensorDesc().getDims();
    size_t batchSize = network.getBatchSize();
    cout << "Batch size " << batchSize << endl;
    cout << inputDims.size() << endl;

    modelChannels = inputDims[1];
    modelHeight = inputDims[2];
    modelWidth = inputDims[3];

    channelSize = modelHeight * modelWidth;
    inputSize = channelSize * modelChannels;

    // Set input info
    (*inputInfo)[*inputName]->setPrecision(Precision::U8);
    (*inputInfo)[*inputName]->setLayout(Layout::NCHW);

    // Get output info
    outputInfo = new OutputsDataMap(network.getOutputsInfo());
    outputName = &(outputInfo->begin()->first);
    outputDims = outputInfo->begin()->second->getDims();

    cout << outputDims.size() << endl;

    // Set output info
    (*outputInfo)[*outputName]->setPrecision(Precision::FP32);
    (*outputInfo)[*outputName]->setLayout(Layout::NC);

    // Load model into plugin
    execNetwork = plugin.LoadNetwork(network, targetDevice);

    // Create inference requests
    inferRequest = execNetwork.CreateInferRequestPtr();
    
    return 0;
}

template <typename T>
void cvMatToBlob(const cv::Mat &orig_image, Blob::Ptr &blob)
{
    SizeVector blobSize = blob.get()->getTensorDesc().getDims();
    size_t channels = blobSize[1];
    size_t height = blobSize[2];
    size_t width = blobSize[3];

    auto resolution = height * width;

    // Get pointer to blob data as type T
    T *blobData = blob->buffer().as<T *>();

    // cv::Mat resized_image(orig_image);
    // if (static_cast<int>(width) != orig_image.size().width ||
    //         static_cast<int>(height) != orig_image.size().height) {
    //     cv::resize(orig_image, resized_image, cv::Size(width, height));
    // }

    // Fill blob
    // for(size_t c = 0; c < channels; c++)
    // {
    //     auto aux = c * resolution;
    //     for(size_t h = 0; h < height; h++)
    //     {
    //         auto aux2 = h * width;
    //         for(size_t w = 0; w < width; w++)
    //         {
    //             blobData[aux + aux2 + w] = orig_image.at<cv::Vec3b>(h, w)[c];
    //         }
    //     }
    // }

    for(size_t c = 0; c < 1; c++)
    {
        for(size_t h = 0; h < resolution; h++)
        {
            for(size_t w = 0; w < channels; w++)
            {
                blobData[c*resolution*channels + w*channels + h] = orig_image.at<cv::Vec3b>(h, w)[c];
            }
        }
    }

    return;
}

void fillInputBlob(cv::Mat img)
{
    Blob::Ptr inputBlob;
    inputBlob = inferRequest->GetBlob(*inputName);

    // cvMatToBlob<uchar>(img, inputBlob);
    SizeVector blobSize = inputBlob.get()->getTensorDesc().getDims();
    size_t channels = blobSize[1];
    size_t height = blobSize[2];
    size_t width = blobSize[3];

    auto resolution = height * width;

    cout << "Filled blob: " << channels << " " << height << " " << width << endl;

    // Get pointer to blob data as type T
    // T *blobData = blob->buffer().as<T *>();
    auto data = inputBlob->buffer().as<PrecisionTrait<Precision::U8>::value_type*>();

    cv::Mat resized_image;
    if (static_cast<int>(width) != img.size().width ||
            static_cast<int>(height) != img.size().height) {
        cv::resize(img, resized_image, cv::Size(width, height));
    }

    // Fill blob
    for(size_t c = 0; c < channels; c++)
    {
        auto aux = c * resolution;
        for(size_t h = 0; h < height; h++)
        {
            auto aux2 = h * width;
            for(size_t w = 0; w < width; w++)
            {
                data[aux + aux2 + w] = resized_image.at<cv::Vec3b>(h, w)[c];
            }
        }
    }

    // cout << "Finished filling blob" << endl;
}

void inferenceRequest()
{
    inferRequest->StartAsync();

    // cout << "Finished requesting inference" << endl;
}

StatusCode wait()
{
    return inferRequest->Wait(IInferRequest::WaitMode::RESULT_READY);
}

float *inference()
{
    // cout << "Before return" << endl;

    return inferRequest->GetBlob(*outputName)->buffer().as<PrecisionTrait<Precision::FP32>::value_type*>();
}

class VideoStreamReceiver {
private:
    string m_thingPropertiesUri;
    DataRiver m_dataRiver = createDataRiver();
    Thing m_thing = createThing();

    DataRiver createDataRiver() {
        return DataRiver::getInstance();
    }

    Thing createThing() {
        // Create and Populate the TagGroup registry with JSON resource files.
        JSonTagGroupRegistry tgr;
        tgr.registerTagGroupsFromURI("file://definitions/TagGroup/com.adlinktech.MQ/VideoStreamTagGroup.json");
        m_dataRiver.addTagGroupRegistry(tgr);

        // Create and Populate the ThingClass registry with JSON resource files.
        JSonThingClassRegistry tcr;
        tcr.registerThingClassesFromURI("file://definitions/ThingClass/com.adlinktech.MQ/VideoStreamReceiverThingClass.json");
        m_dataRiver.addThingClassRegistry(tcr);

        // Create a Thing based on properties specified in a JSON resource file.
        JSonThingProperties tp;
        tp.readPropertiesFromURI(m_thingPropertiesUri);

        return m_dataRiver.createThing(tp);
    }

public:
    VideoStreamReceiver(string thingPropertiesUri) : m_thingPropertiesUri(thingPropertiesUri) {
        cout << "Video Stream Receiver started" << endl;
    }

    ~VideoStreamReceiver() {
        m_dataRiver.close();
        cout << "Video Stream Receiver stopped" << endl;
    }

    int run(int runningTime) {
        long long elapsedSeconds = 0;
        //cv::Mat incomeMat(cv::Size(256, 256), CV_8UC1);

        // network_reader.ReadNetwork("/home/adlink/Desktop/Numbs/model/handwritten.xml");
        // network_reader.ReadWeights("/home/adlink/Desktop/Numbs/model/handwritten.bin");

        // auto network = network_reader.getNetwork();

        // // Taking information about all topology inputs
        // InputsDataMap input_info(network.getInputsInfo());
        // OutputsDataMap output_info(network.getOutputsInfo());

        // cout << "Input infos: " + input_info.size() << endl;
        // cout << "Ouput infos: " + output_info.size() << endl;

        // auto executable_network = core.LoadNetwork(network, "MYRIAD");

        // auto infer_request = executable_network.CreateInferRequest();

        // // Iterating over all input blobs
        // for(auto & item : input_info)
        // {
        //     auto input_name = item.first;
        //     // Getting input blob
        //     auto input = infer_request.GetBlob(input_name);
        // }

        string modelNetwork = "/home/adlink/Desktop/Numbs/model/v2/handwritten_uint8.xml";
        string modelWeights = "/home/adlink/Desktop/Numbs/model/v2/handwritten_uint8.bin";
        string targetDevice = "MYRIAD";

        loadNetwork(modelNetwork, modelWeights, core, targetDevice);

        cout << "Input name: " + *inputName << endl;
        cout << "Input info: " << inputInfo->size() << endl;
        cout << "Input dims: " << inputDims.size() << " [" << inputDims[0] << "," << inputDims[1] << "," << inputDims[2] << "," << inputDims[3] << "]" << endl;

        cout << endl;
        
        cout << "Output name: " + *outputName << endl;
        cout << "Output info: " << outputInfo->size() << endl;
        cout << "Output dims: " << outputDims.size() << " [" << outputDims[0] << "," << outputDims[1] << "]" << endl;

        /** This vector stores id's of top N results **/
        vector<string> resultsID{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        vector<string> imageNames{"Frame"};

        while(1) {
            // Read all data for input 'temperature'
            vector<DataSample<IOT_NVP_SEQ> > msgs = m_thing.read<IOT_NVP_SEQ>("video");

            for (const DataSample<IOT_NVP_SEQ>& msg : msgs) {
                auto flowState = msg.getFlowState();
                if (flowState == FlowState::ALIVE) {
                    const IOT_NVP_SEQ& dataSample = msg.getData();
                    try {
                        for (const IOT_NVP& nvp : dataSample) {
                            if (nvp.name() == "video") {
                                // Buffer = nvp.value().iotv_byte_seq();
                                //incomeMat.data = Buffer;
                                cv::Mat incomeMat(nvp.value().iotv_byte_seq());
                                //cout << incomeMat.total() << endl;
                                frame = incomeMat.reshape(1, 480);
                                // cout << frame.col(0).total() << endl;
                                // cv::resize(incomeMat, frame, cv::Size(720, 720));
                            }
                            cout << "Frame total: " << frame.total() << endl;
                            // cout << "1" << endl;
                            fillInputBlob(frame);
                            // Blob::Ptr imgBlob = wrapMat2Blob(frame);
                            // inferRequest->SetBlob(*inputName, imgBlob);
                            // cout << "2" << endl;
                            // inferenceRequest();
                            inferRequest->Infer();
                            // cout << "3" << endl;
                            if(wait() == 0)
                            {
                            Blob::Ptr outputBlob = inferRequest->GetBlob(*outputName);
                        
                            ClassificationResult classificationResult(outputBlob, imageNames, 1, 5, resultsID);
                            classificationResult.print();
                            }
                            // if(wait() == 0)
                            // {
                            //     float *results = inference();
                            //     float *result = results + objectSize;
                            //     cout << objectSize << endl;
                            //     // cout << "4" << endl;
                            //     cout << "Prediction:\t" << results[0] << "\t\t"
                            //                             << results[1] << "\t\t"
                            //                             << results[2] << "\t\t"
                            //                             << results[3] << "\t\t"
                            //                             << results[4] << "\t\t"
                            //                             << results[5] << "\t\t"
                            //                             << results[6] << "\t\t"
                            //                             << results[7] << "\t\t"
                            //                             << results[8] << "\t\t" 
                            //                             << results[9] << endl;

                            //     cout.precision(7);
                            //     /** Getting probability for resulting class **/
                            //     const auto result = outputBlob->buffer().
                            //             as<InferenceEngine::PrecisionTrait<InferenceEngine::Precision::FP16>::value_type*>()
                            //     [results[id] + image_id * (_outBlob->size() / _batchSize)];
                            // }
                            cv::imshow("Video Captured", frame);
                        }
                    }
                    catch (exception& e) {
                        cerr << "An unexpected error occured while processing data-sample: " << e.what() << endl;
                        continue;
                    }
                }
            }
            if (cv::waitKey(5) == 27)
                break;
        }
        cv::destroyAllWindows();

        return 0;
    }
};

int main(int argc, char *argv[]) {
    // Get thing properties URI from command line parameter
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " THING_PROPERTIES_URI RUNNING_TIME" << endl;
        exit(1);
    }
    string thingPropertiesUri = string(argv[1]);
    int runningTime = atoi(argv[2]);

    try {
        VideoStreamReceiver(thingPropertiesUri).run(runningTime);
    }
    catch (ThingAPIException& e) {
        cerr << "An unexpected error occurred: " << e.what() << endl;
    }catch(std::exception& e1){
        cerr << "An unexpected error occurred: " << e1.what() << endl;
    }
}
