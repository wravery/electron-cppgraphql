#include <nan.h>
#include <iostream>
#include <graphqlservice/JSONResponse.h>

#include "Today.h"

using v8::Function;
using v8::FunctionTemplate;
using v8::Local;
using v8::String;
using v8::Value;
using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::Callback;
using Nan::DecodeWrite;
using Nan::Encoding;
using Nan::GetFunction;
using Nan::HandleScope;
using Nan::New;
using Nan::Null;
using Nan::Set;
using Nan::To;

using namespace facebook::graphql;

NAN_METHOD(HelloMethod)
{
    info.GetReturnValue().Set(New<String>("Native World").ToLocalChecked());
}

static std::vector<uint8_t> binAppointmentId;
static std::vector<uint8_t> binTaskId;
static std::vector<uint8_t> binFolderId;
static std::shared_ptr<today::Operations> serviceSingleton;

NAN_METHOD(StartService)
{
	std::string fakeAppointmentId("fakeAppointmentId");
	binAppointmentId.resize(fakeAppointmentId.size());
	std::copy(fakeAppointmentId.cbegin(), fakeAppointmentId.cend(), binAppointmentId.begin());

	std::string fakeTaskId("fakeTaskId");
	binTaskId.resize(fakeTaskId.size());
	std::copy(fakeTaskId.cbegin(), fakeTaskId.cend(), binTaskId.begin());

	std::string fakeFolderId("fakeFolderId");
	binFolderId.resize(fakeFolderId.size());
	std::copy(fakeFolderId.cbegin(), fakeFolderId.cend(), binFolderId.begin());

	auto query = std::make_shared<today::Query>(
		[]() -> std::vector<std::shared_ptr<today::Appointment>>
	{
		return { std::make_shared<today::Appointment>(std::move(binAppointmentId), "tomorrow", "Lunch?", false) };
	}, []() -> std::vector<std::shared_ptr<today::Task>>
	{
		return { std::make_shared<today::Task>(std::move(binTaskId), "Don't forget", true) };
	}, []() -> std::vector<std::shared_ptr<today::Folder>>
	{
		return { std::make_shared<today::Folder>(std::move(binFolderId), "\"Fake\" Inbox", 3) };
	});
	auto mutation = std::make_shared<today::Mutation>(
		[](today::CompleteTaskInput&& input) -> std::shared_ptr<today::CompleteTaskPayload>
	{
		return std::make_shared<today::CompleteTaskPayload>(
			std::make_shared<today::Task>(std::move(input.id), "Mutated Task!", *(input.isComplete)),
			std::move(input.clientMutationId)
		);
	});
	auto subscription = std::make_shared<today::Subscription>();
	
    serviceSingleton = std::make_shared<today::Operations>(query, mutation, subscription);
}

NAN_METHOD(StopService)
{
    auto cleanupCapturedParams = today::NestedType::getCapturedParams();
    serviceSingleton.reset();
}

class FetchWorker : public AsyncWorker
{
public:
    explicit FetchWorker(Callback* callback, std::string&& query, std::string&& operationName, const std::string& variables)
        : AsyncWorker(callback)
        , _operationName(std::move(operationName))
    {
        try
        {
            _ast_input = peg::parseString(std::move(query));
            _variables = (variables.empty() ? response::Value(response::Type::Map) : response::parseJSON(variables));

            if (_variables.type() != response::Type::Map)
            {
                throw std::runtime_error("Invalid variables object");
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Caught exception preparing the query: " << ex.what();

            response::Value document(response::Type::Map);

            document.emplace_back("data", response::Value());
            document.emplace_back("errors", response::Value(ex.what()));

            _response = response::toJSON(std::move(document));
        }
    }

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute() override
    {
        if (!_response.empty())
        {
            // We caught an exception in the constructor, don't overwrite the error.
            return;
        }

        try
        {
            if (!serviceSingleton)
            {
                throw std::runtime_error("The service is not started!");
            }

            _response = response::toJSON(serviceSingleton->resolve(nullptr, *_ast_input->root.get(), _operationName, std::move(_variables)).get());
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Caught exception executing the query: " << ex.what();

            response::Value document(response::Type::Map);

            document.emplace_back("data", response::Value());
            document.emplace_back("errors", response::Value(ex.what()));

            _response = response::toJSON(std::move(document));
        }
    }

    // Executed when the async work is complete
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleOKCallback() override
    {
        HandleScope scope;
        
        Local<Value> argv[] = {
            Null(),
            New<String>(_response.c_str()).ToLocalChecked()
        };

        callback->Call(2, argv, async_resource);
    }

    void HandleErrorCallback() override
    {
        HandleScope scope;
        
        Local<Value> argv[] = {
            New<String>(ErrorMessage()).ToLocalChecked(),
            Null()
        };

        callback->Call(2, argv, async_resource);
    }

private:
    std::unique_ptr<peg::ast<std::string>> _ast_input;
    std::string _operationName;
    response::Value _variables;
    std::string _response;
};

NAN_METHOD(FetchQuery) {
    std::string query(*Nan::Utf8String(To<String>(info[0]).ToLocalChecked()));
    std::string operationName(*Nan::Utf8String(To<String>(info[1]).ToLocalChecked()));
    std::string variables(*Nan::Utf8String(To<String>(info[2]).ToLocalChecked()));
    Callback* callback = new Callback(To<Function>(info[3]).ToLocalChecked());

    AsyncQueueWorker(new FetchWorker(callback, std::move(query), std::move(operationName), variables));
}

NAN_MODULE_INIT(Init) {
    Set(target, New<String>("nativeHello").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(HelloMethod)).ToLocalChecked());

    Set(target, New<String>("startService").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(StartService)).ToLocalChecked());

    Set(target, New<String>("stopService").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(StopService)).ToLocalChecked());

    Set(target, New<String>("fetchQuery").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(FetchQuery)).ToLocalChecked());
}

NODE_MODULE(cppgraphql, Init)