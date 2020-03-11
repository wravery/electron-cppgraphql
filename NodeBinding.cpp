#include <nan.h>

#include <iostream>
#include <memory>
#include <map>
#include <thread>
#include <queue>

#include "graphqlservice/JSONResponse.h"

#include "TodayMock.h"

using v8::Function;
using v8::FunctionTemplate;
using v8::Promise;
using v8::Local;
using v8::String;
using v8::Int32;
using v8::Value;
using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::AsyncProgressWorkerBase;
using Nan::Callback;
using Nan::DecodeWrite;
using Nan::Encoding;
using Nan::GetFunction;
using Nan::HandleScope;
using Nan::New;
using Nan::Null;
using Nan::Set;
using Nan::To;

using namespace graphql;

static std::shared_ptr<today::Appointment> appointment;
static std::shared_ptr<today::Task> task;
static std::shared_ptr<today::Folder> folder;

static std::map<response::IdType, std::shared_ptr<service::Object>> nodes;

static std::shared_ptr<today::Operations> serviceSingleton;

void loadAppointments()
{
	std::string fakeAppointmentId("fakeAppointmentId");
    response::IdType binAppointmentId(fakeAppointmentId.size());
	std::copy(fakeAppointmentId.cbegin(), fakeAppointmentId.cend(), binAppointmentId.begin());

    appointment = std::make_shared<today::Appointment>(std::move(binAppointmentId), "tomorrow", "Lunch?", false);

    nodes[appointment->id()] = appointment;
};

void loadTasks()
{
	std::string fakeTaskId("fakeTaskId");
	response::IdType binTaskId(fakeTaskId.size());
	std::copy(fakeTaskId.cbegin(), fakeTaskId.cend(), binTaskId.begin());

    task = std::make_shared<today::Task>(std::move(binTaskId), "Don't forget", true);

    nodes[task->id()] = task;
}

void loadUnreadCounts()
{
	std::string fakeFolderId("fakeFolderId");
	response::IdType binFolderId(fakeFolderId.size());
	std::copy(fakeFolderId.cbegin(), fakeFolderId.cend(), binFolderId.begin());

    folder = std::make_shared<today::Folder>(std::move(binFolderId), "\"Fake\" Inbox", 3);

    nodes[folder->id()] = folder;
}

class MockSubscription : public today::object::Subscription
{
public:
	explicit MockSubscription() = default;

	service::FieldResult<std::shared_ptr<today::object::Appointment>> getNextAppointmentChange(service::FieldParams&&) const final
	{
		throw std::runtime_error("Unexpected call to getNextAppointmentChange");
	}

	service::FieldResult<std::shared_ptr<service::Object>> getNodeChange(service::FieldParams&&, response::IdType&& nodeId) const final
	{
        auto itr = nodes.find(nodeId);

        return itr == nodes.end()
            ? std::shared_ptr<service::Object>{}
            : itr->second;
	}
};

static std::shared_ptr<MockSubscription> mockSubscription;

class MockMutation : public today::object::Mutation
{
public:
    explicit MockMutation()
    {
    }

    service::FieldResult<std::shared_ptr<service::Object>> applyChangeNode(service::FieldParams&& params, response::IdType&& idArg) const final
    {
        auto itr = nodes.find(idArg);

        if (itr == nodes.end())
        {
            return nullptr;
        }

        service::SubscriptionArguments arguments;

        arguments["id"] = response::Value(service::Base64::toBase64(idArg));
        serviceSingleton->deliver(std::launch::async, { "nodeChange" }, std::move(arguments), mockSubscription);

        return itr->second;
    }
};

NAN_METHOD(StartService)
{
    loadAppointments();
    loadTasks();
    loadUnreadCounts();

	auto query = std::make_shared<today::Query>(
        []() -> std::vector<std::shared_ptr<today::Appointment>>
    {
        return { appointment };
    }, []() -> std::vector<std::shared_ptr<today::Task>>{
        return { task };
    }, []() -> std::vector<std::shared_ptr<today::Folder>>
    {
        return { folder };
    });
	auto mutation = std::make_shared<MockMutation>();
	mockSubscription = std::make_shared<MockSubscription>();
	
    serviceSingleton = std::make_shared<today::Operations>(query, mutation, mockSubscription);
}

struct SubscriptionPayloadQueue : std::enable_shared_from_this<SubscriptionPayloadQueue>
{
    ~SubscriptionPayloadQueue()
    {
        Unsubscribe();
    }

    void Unsubscribe()
    {
        std::optional<service::SubscriptionKey> deferUnsubscribe;
        std::unique_lock<std::mutex> lock(mutex);

        if (!registered)
        {
            return;
        }

        registered = false;
        deferUnsubscribe = std::make_optional(key);
        key = {};

        lock.unlock();
        condition.notify_one();

        if (deferUnsubscribe
            && serviceSingleton)
        {
            serviceSingleton->unsubscribe(*deferUnsubscribe);
        }
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::queue<std::future<response::Value>> payloads;
    service::SubscriptionKey key {};
    bool registered = false;
};

static std::map<int32_t, std::shared_ptr<SubscriptionPayloadQueue>> subscriptionMap;

NAN_METHOD(StopService)
{
    if (serviceSingleton)
    {
        for (const auto& entry : subscriptionMap)
        {
            entry.second->Unsubscribe();
        }

        subscriptionMap.clear();
        serviceSingleton.reset();
    }
}

constexpr uint32_t c_resolverIndex = 1;

class FetchWorker : public AsyncWorker
{
public:
    explicit FetchWorker(std::string&& query, std::string&& operationName, const std::string& variables, const v8::Local<v8::Promise::Resolver>& resolver)
        : AsyncWorker(nullptr, "graphql:fetchQuery")
        , _operationName(std::move(operationName))
    {
        SaveToPersistent(c_resolverIndex, resolver);

        try
        {
            _ast = peg::parseString(query);
            _variables = (variables.empty() ? response::Value(response::Type::Map) : response::parseJSON(variables));

            if (_variables.type() != response::Type::Map)
            {
                throw std::runtime_error("Invalid variables object");
            }
        }
        catch (const std::exception& ex)
        {
            std::ostringstream oss;

            oss << "Caught exception preparing the query: " << ex.what();

            SetErrorMessage(oss.str().c_str());
        }
    }

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute() override
    {
        if (ErrorMessage() != nullptr)
        {
            // We caught an exception in the constructor, return the error message or a well formed
            // collection of GraphQL errors.
            return;
        }

        try
        {
            if (!serviceSingleton)
            {
                throw std::runtime_error("The service is not started!");
            }

            _response = response::toJSON(serviceSingleton->resolve(nullptr, _ast, _operationName, std::move(_variables)).get());
        }
        catch (service::schema_exception& scx)
		{
			response::Value document(response::Type::Map);

			document.emplace_back(std::string { service::strData }, response::Value());
			document.emplace_back(std::string { service::strErrors }, scx.getErrors());

            _response = response::toJSON(std::move(document));
		}
        catch (const std::exception& ex)
        {
            std::ostringstream oss;

            oss << "Caught exception executing the query: " << ex.what();

            SetErrorMessage(oss.str().c_str());
        }
    }

    // Executed when the async work is complete
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleOKCallback() override
    {
        HandleScope scope;
        auto resolver = GetFromPersistent(c_resolverIndex).As<v8::Promise::Resolver>();

        resolver->Resolve(Nan::GetCurrentContext(), New<String>(_response.c_str()).ToLocalChecked());
    }

    void HandleErrorCallback() override
    {
        HandleScope scope;
        auto resolver = GetFromPersistent(c_resolverIndex).As<v8::Promise::Resolver>();

        resolver->Reject(Nan::GetCurrentContext(), New<String>(ErrorMessage()).ToLocalChecked());
    }

private:
    peg::ast _ast;
    std::string _operationName;
    response::Value _variables;
    std::string _response;
};

NAN_METHOD(FetchQuery) {
    std::string query(*Nan::Utf8String(To<String>(info[0]).ToLocalChecked()));
    std::string operationName(*Nan::Utf8String(To<String>(info[1]).ToLocalChecked()));
    std::string variables(*Nan::Utf8String(To<String>(info[2]).ToLocalChecked()));
    auto resolver = v8::Promise::Resolver::New(info.GetIsolate()->GetCurrentContext()).ToLocalChecked();

    AsyncQueueWorker(new FetchWorker(std::move(query), std::move(operationName), variables, resolver));

    info.GetReturnValue().Set(resolver->GetPromise());
}

class RegisteredSubscription : public AsyncProgressWorkerBase<std::string>
{
public:
    explicit RegisteredSubscription(std::string&& query, std::string&& operationName, const std::string& variables, Callback* callback)
        : AsyncProgressWorkerBase(callback, "graphql:subscription")
    {
        try
        {
            auto ast = peg::parseString(query);
            auto parsedVariables = (variables.empty() ? response::Value(response::Type::Map) : response::parseJSON(variables));

            if (parsedVariables.type() != response::Type::Map)
            {
                throw std::runtime_error("Invalid variables object");
            }

            _payloadQueue = std::make_shared<SubscriptionPayloadQueue>();

            std::weak_ptr<SubscriptionPayloadQueue> wpQueue { _payloadQueue };

            _key = serviceSingleton->subscribe(service::SubscriptionParams {
                nullptr,
                std::move(ast),
                std::move(operationName),
                std::move(parsedVariables)
            }, [spQueue = _payloadQueue](std::future<response::Value> payload) noexcept -> void
            {
                std::unique_lock<std::mutex> lock(spQueue->mutex);

                if (!spQueue->registered)
                {
                    return;
                }

                spQueue->payloads.push(std::move(payload));

                lock.unlock();
                spQueue->condition.notify_one();
            });

            _payloadQueue->registered = true;
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Caught exception preparing the subscription: " << ex.what() << std::endl;
        }
    }

    ~RegisteredSubscription()
    {
        if (_payloadQueue)
        {
            _payloadQueue->Unsubscribe();
        }
    }

    const std::shared_ptr<SubscriptionPayloadQueue>& GetPayloadQueue() const
    {
        return _payloadQueue;
    }

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute(const ExecutionProgress& progress) override
    {
        auto spQueue = _payloadQueue;

        while (spQueue)
        {
            std::unique_lock<std::mutex> lock(spQueue->mutex);

            spQueue->condition.wait(lock, [spQueue]() noexcept -> bool
            {
                return !spQueue->registered
                    || !spQueue->payloads.empty();
            });

            if (!spQueue->registered)
            {
                break;
            }

            auto payload = std::move(spQueue->payloads.front());

            spQueue->payloads.pop();
            lock.unlock();

            try
            {
                auto result = response::toJSON(payload.get());

                progress.Send(&result, 1);
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;

                oss << "Caught exception delivering subscription payload: " << ex.what();

                SetErrorMessage(oss.str().c_str());
            }
        }
    }

    // Executed when the async results are ready
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleProgressCallback(const std::string* data, size_t size) override
    {
        if (data == nullptr)
        {
            return;
        }

        HandleScope scope;

        while (size-- > 0)
        {
            Local<Value> argv[] = {
                New<String>(data->c_str(), static_cast<int>(data->size())).ToLocalChecked()
            };

            callback->Call(1, argv, async_resource);
            ++data;
        }
    }

private:
    std::shared_ptr<SubscriptionPayloadQueue> _payloadQueue;
    service::SubscriptionKey _key {};
};

NAN_METHOD(Subscribe) {
    std::string query(*Nan::Utf8String(To<String>(info[0]).ToLocalChecked()));
    std::string operationName(*Nan::Utf8String(To<String>(info[1]).ToLocalChecked()));
    std::string variables(*Nan::Utf8String(To<String>(info[2]).ToLocalChecked()));
    auto callback = std::make_unique<Callback>(To<Function>(info[3]).ToLocalChecked());
    const int32_t index = (subscriptionMap.empty() ? 1 : subscriptionMap.crbegin()->first + 1);
    auto subscription = std::make_unique<RegisteredSubscription>(std::move(query), std::move(operationName), variables, callback.get());

    callback.release();
    subscriptionMap[index] = subscription->GetPayloadQueue();
    AsyncQueueWorker(subscription.release());

    info.GetReturnValue().Set(index);
}

NAN_METHOD(Unsubscribe) {
    const int32_t index = To<Int32>(info[0]).ToLocalChecked()->Int32Value();
    auto itr = subscriptionMap.find(index);

    if (itr != subscriptionMap.end())
    {
        itr->second->Unsubscribe();
        subscriptionMap.erase(itr);
    }
}

NAN_MODULE_INIT(Init) {
    Set(target, New<String>("startService").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(StartService)).ToLocalChecked());

    Set(target, New<String>("stopService").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(StopService)).ToLocalChecked());

    Set(target, New<String>("fetchQuery").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(FetchQuery)).ToLocalChecked());

    Set(target, New<String>("subscribe").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(Subscribe)).ToLocalChecked());

    Set(target, New<String>("unsubscribe").ToLocalChecked(),
        GetFunction(New<FunctionTemplate>(Unsubscribe)).ToLocalChecked());
}

NODE_MODULE(cppgraphql, Init)