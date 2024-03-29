#include "graphqlservice/JSONResponse.h"

#include "TodayMock.h"

#include <nan.h>

#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <thread>

using Nan::AsyncProgressQueueWorker;
using Nan::AsyncQueueWorker;
using Nan::Callback;
using Nan::DecodeWrite;
using Nan::Encoding;
using Nan::GetFunction;
using Nan::HandleScope;
using Nan::New;
using Nan::Null;
using Nan::Set;
using Nan::To;
using v8::Function;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Local;
using v8::Promise;
using v8::String;
using v8::Value;

using namespace graphql;

static std::shared_ptr<today::Appointment> appointment;
static std::shared_ptr<today::Task> task;
static std::shared_ptr<today::Folder> folder;

static std::map<response::IdType, std::shared_ptr<today::object::Node>> nodes;

static std::shared_ptr<today::Operations> serviceSingleton;

void loadAppointments()
{
	std::string fakeAppointmentId("fakeAppointmentId");
	response::IdType binAppointmentId(fakeAppointmentId.size());
	std::copy(fakeAppointmentId.cbegin(), fakeAppointmentId.cend(), binAppointmentId.begin());

	appointment = std::make_shared<today::Appointment>(std::move(binAppointmentId),
		"tomorrow",
		"Lunch?",
		false);

	nodes[appointment->id()] = std::make_shared<today::object::Node>(
		std::make_shared<today::object::Appointment>(appointment));
};

void loadTasks()
{
	std::string fakeTaskId("fakeTaskId");
	response::IdType binTaskId(fakeTaskId.size());
	std::copy(fakeTaskId.cbegin(), fakeTaskId.cend(), binTaskId.begin());

	task = std::make_shared<today::Task>(std::move(binTaskId), "Don't forget", true);

	nodes[task->id()] =
		std::make_shared<today::object::Node>(std::make_shared<today::object::Task>(task));
}

void loadUnreadCounts()
{
	std::string fakeFolderId("fakeFolderId");
	response::IdType binFolderId(fakeFolderId.size());
	std::copy(fakeFolderId.cbegin(), fakeFolderId.cend(), binFolderId.begin());

	folder = std::make_shared<today::Folder>(std::move(binFolderId), "\"Fake\" Inbox", 3);

	nodes[folder->id()] =
		std::make_shared<today::object::Node>(std::make_shared<today::object::Folder>(folder));
}

class MockSubscription
{
public:
	explicit MockSubscription() = default;

	std::shared_ptr<today::object::Appointment> getNextAppointmentChange() const
	{
		throw std::runtime_error("Unexpected call to getNextAppointmentChange");
	}

	std::shared_ptr<today::object::Node> getNodeChange(response::IdType&& nodeId) const
	{
		auto itr = nodes.find(nodeId);

		return itr == nodes.end() ? std::shared_ptr<today::object::Node> {} : itr->second;
	}
};

NAN_METHOD(startService)
{
	loadAppointments();
	loadTasks();
	loadUnreadCounts();

	auto query = std::make_shared<today::Query>(
		[]() -> std::vector<std::shared_ptr<today::Appointment>> {
			return { appointment };
		},
		[]() -> std::vector<std::shared_ptr<today::Task>> {
			return { task };
		},
		[]() -> std::vector<std::shared_ptr<today::Folder>> {
			return { folder };
		});
	auto mutation = std::make_shared<today::Mutation>(
		[](today::CompleteTaskInput&& input) -> std::shared_ptr<today::CompleteTaskPayload> {
			auto itr = nodes.find(input.id);

			if (itr == nodes.end())
			{
				return nullptr;
			}

			service::SubscriptionArguments arguments;

			arguments["id"] = response::Value(std::move(input.id));
			serviceSingleton
				->deliver({ "nodeChange",
					{ service::SubscriptionFilter { { std::move(arguments) } } },
					std::launch::async,
					std::make_shared<today::object::Subscription>(
						std::make_shared<MockSubscription>()) })
				.get();

			return std::make_shared<today::CompleteTaskPayload>(task,
				std::move(input.clientMutationId));
		});

	serviceSingleton = std::make_shared<today::Operations>(std::move(query),
		std::move(mutation),
		std::shared_ptr<today::Subscription> {});
}

struct SubscriptionPayloadQueue : std::enable_shared_from_this<SubscriptionPayloadQueue>
{
	~SubscriptionPayloadQueue()
	{
		Unsubscribe();
	}

	void Unsubscribe()
	{
		std::unique_lock<std::mutex> lock(mutex);

		if (!registered)
		{
			return;
		}

		registered = false;

		auto deferUnsubscribe = std::move(key);

		lock.unlock();
		condition.notify_one();

		if (deferUnsubscribe && serviceSingleton)
		{
			serviceSingleton->unsubscribe({ *deferUnsubscribe }).get();
		}
	}

	std::mutex mutex;
	std::condition_variable condition;
	std::queue<response::AwaitableValue> payloads;
	std::optional<service::SubscriptionKey> key;
	bool registered = false;
};

static std::map<std::int32_t, peg::ast> queryMap;
static std::map<std::int32_t, std::shared_ptr<SubscriptionPayloadQueue>> subscriptionMap;

NAN_METHOD(stopService)
{
	if (serviceSingleton)
	{
		for (const auto& entry : subscriptionMap)
		{
			entry.second->Unsubscribe();
		}

		subscriptionMap.clear();
		queryMap.clear();
		serviceSingleton.reset();
	}
}

NAN_METHOD(parseQuery)
{
	std::string query(*Nan::Utf8String(To<String>(info[0]).ToLocalChecked()));
	const std::int32_t queryId = (queryMap.empty() ? 1 : queryMap.crbegin()->first + 1);

	try
	{
		auto ast = peg::parseString(query);
		auto validationErrors = serviceSingleton->validate(ast);

		if (!validationErrors.empty())
		{
			throw service::schema_exception { std::move(validationErrors) };
		}

		queryMap[queryId] = std::move(ast);
		info.GetReturnValue().Set(New<Int32>(queryId));
	}
	catch (const std::exception& ex)
	{
		Nan::ThrowError(ex.what());
	}
}

NAN_METHOD(discardQuery)
{
	const auto queryId = To<std::int32_t>(info[0]).FromJust();

	queryMap.erase(queryId);
}

class RegisteredSubscription : public AsyncProgressQueueWorker<std::string>
{
public:
	explicit RegisteredSubscription(std::int32_t queryId, std::string&& operationName,
		const std::string& variables, std::unique_ptr<Callback>&& next,
		std::unique_ptr<Callback>&& complete)
		: AsyncProgressQueueWorker(complete.release(), "graphql:subscription")
		, _next { std::move(next) }
	{
		try
		{
			_payloadQueue = std::make_shared<SubscriptionPayloadQueue>();

			const auto itrQuery = queryMap.find(queryId);

			if (itrQuery == queryMap.cend())
			{
				throw std::runtime_error("Unknown queryId");
			}

			auto& ast = itrQuery->second;
			auto parsedVariables = (variables.empty() ? response::Value(response::Type::Map)
													  : response::parseJSON(variables));

			if (parsedVariables.type() != response::Type::Map)
			{
				throw std::runtime_error("Invalid variables object");
			}

			std::unique_lock<std::mutex> lock(_payloadQueue->mutex);

			if (serviceSingleton->findOperationDefinition(ast, operationName).first
				== service::strSubscription)
			{
				_payloadQueue->registered = true;
				_payloadQueue->key = std::make_optional(
					serviceSingleton
						->subscribe(
							{ [spQueue = _payloadQueue](response::Value payload) noexcept -> void {
								 std::unique_lock<std::mutex> lock(spQueue->mutex);

								 if (!spQueue->registered)
								 {
									 return;
								 }

								 std::promise<response::Value> promise;

								 promise.set_value(std::move(payload));
								 spQueue->payloads.push(promise.get_future());

								 lock.unlock();
								 spQueue->condition.notify_one();
							 },
								peg::ast { ast },
								std::move(operationName),
								std::move(parsedVariables) })
						.get());
			}
			else
			{
				_payloadQueue->payloads.push(
					serviceSingleton->resolve({ ast, operationName, std::move(parsedVariables) }));

				lock.unlock();
				_payloadQueue->condition.notify_one();
			}
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

private:
	// Executed inside the worker-thread.
	// It is not safe to access V8, or V8 data structures
	// here, so everything we need for input and output
	// should go on `this`.
	void Execute(const ExecutionProgress& progress) override
	{
		auto spQueue = _payloadQueue;
		bool registered = true;

		while (registered)
		{
			std::unique_lock<std::mutex> lock(spQueue->mutex);

			spQueue->condition.wait(lock, [spQueue]() noexcept -> bool {
				return !spQueue->registered || !spQueue->payloads.empty();
			});

			auto payloads = std::move(spQueue->payloads);

			registered = spQueue->registered;
			lock.unlock();

			std::vector<std::string> json;

			while (!payloads.empty())
			{
				response::Value document { response::Type::Map };
				auto payload = std::move(payloads.front());

				payloads.pop();

				try
				{
					document = payload.get();
				}
				catch (service::schema_exception& scx)
				{
					document.reserve(2);
					document.emplace_back(std::string { service::strData }, {});
					document.emplace_back(std::string { service::strErrors }, scx.getErrors());
				}
				catch (const std::exception& ex)
				{
					std::ostringstream oss;

					oss << "Caught exception delivering subscription payload: " << ex.what();
					document.reserve(2);
					document.emplace_back(std::string { service::strData }, {});
					document.emplace_back(std::string { service::strErrors },
						response::Value { oss.str() });
				}

				json.push_back(response::toJSON(std::move(document)));
			}

			if (!json.empty())
			{
				progress.Send(json.data(), json.size());
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

			_next->Call(1, argv, async_resource);
			++data;
		}
	}

	std::unique_ptr<Callback> _next;
	std::shared_ptr<SubscriptionPayloadQueue> _payloadQueue;
};

NAN_METHOD(fetchQuery)
{
	const auto queryId = To<std::int32_t>(info[0]).FromJust();
	std::string operationName(*Nan::Utf8String(To<String>(info[1]).ToLocalChecked()));
	std::string variables(*Nan::Utf8String(To<String>(info[2]).ToLocalChecked()));
	auto next = std::make_unique<Callback>(To<Function>(info[3]).ToLocalChecked());
	auto complete = std::make_unique<Callback>(To<Function>(info[4]).ToLocalChecked());
	auto subscription = std::make_unique<RegisteredSubscription>(queryId,
		std::move(operationName),
		variables,
		std::move(next),
		std::move(complete));

	subscriptionMap[queryId] = subscription->GetPayloadQueue();
	AsyncQueueWorker(subscription.release());
}

NAN_METHOD(unsubscribe)
{
	const auto queryId = To<std::int32_t>(info[0]).FromJust();
	auto itr = subscriptionMap.find(queryId);

	if (itr != subscriptionMap.end())
	{
		itr->second->Unsubscribe();
		subscriptionMap.erase(itr);
	}
}

NAN_MODULE_INIT(Init)
{
	NAN_EXPORT(target, startService);
	NAN_EXPORT(target, stopService);
	NAN_EXPORT(target, parseQuery);
	NAN_EXPORT(target, discardQuery);
	NAN_EXPORT(target, fetchQuery);
	NAN_EXPORT(target, unsubscribe);
}

NODE_MODULE(cppgraphql, Init)