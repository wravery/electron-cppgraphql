describe("GraphQL native module tests", () => {
  const graphql = require(`./build/${
    process.env.USE_DEBUG_MODULE ? "Debug" : "Release"
  }/electron-cppgraphql`);

  it("starts the service", () => {
    graphql.startService();
  });

  let queryId = null;

  it("parses the introspection query", () => {
    queryId = graphql.parseQuery(`query {
        __schema {
            queryType {
                name
            }
            mutationType {
                name
            }
            subscriptionType {
                name
            }
            types {
                kind
                name
            }
        }
    }`);
    expect(queryId).not.toBeNull();
  });

  it("fetches introspection", async () => {
    expect(queryId).not.toBeNull();
    return expect(
      new Promise((resolve) => {
        let result = null;
        graphql.fetchQuery(
          queryId,
          "",
          "",
          (payload) => {
            result = JSON.parse(payload);
          },
          () => {
            resolve(result);
          }
        );
      })
    ).resolves.toMatchSnapshot();
  });

  it("cleans up after the query", () => {
    expect(queryId).not.toBeNull();
    graphql.unsubscribe(queryId);
    graphql.discardQuery(queryId);
    queryId = null;
  });

  let subscriptionId = null;

  it("parses subscription", () => {
    subscriptionId = graphql.parseQuery(`subscription {
        nodeChange(id: "ZmFrZVRhc2tJZA==") {
            id
            ...on Task {
                title
            }
        }
    }`);
    expect(subscriptionId).not.toBeNull();
  });

  let subscriptionPromise = null;
  let completedPromise = null;
  let completed = false;

  it("registers subscription", async () => {
    expect(subscriptionId).not.toBeNull();
    let resolveCompleted = null;
    completedPromise = new Promise((resolve) => {
      resolveCompleted = resolve;
    });
    subscriptionPromise = new Promise((resolve) => {
      graphql.fetchQuery(
        subscriptionId,
        "",
        "",
        (payload) => {
          resolve(JSON.parse(payload));
        },
        () => {
          completed = true;
          resolveCompleted();
        }
      );
    });
    expect(subscriptionPromise).not.toBeNull();
    expect(completed).toEqual(false);
  });

  let mutationId = null;

  it("parses mutation", () => {
    mutationId = graphql.parseQuery(`mutation {
        completedTask: completeTask(input: {id: "ZmFrZVRhc2tJZA==", isComplete: true, clientMutationId: "Hi There!"}) {
            completedTask: task {
                completedTaskId: id
                title
                isComplete
            }
            clientMutationId
        }
    }`);
    expect(mutationId).not.toBeNull();
  });

  it("applies mutation", async () => {
    expect(mutationId).not.toBeNull();
    return expect(
      new Promise((resolve) => {
        let result = null;
        graphql.fetchQuery(
          mutationId,
          "",
          "",
          (payload) => {
            result = JSON.parse(payload);
          },
          () => {
            resolve(result);
          }
        );
      })
    ).resolves.toMatchSnapshot();
  });

  it("cleans up after the mutation", () => {
    expect(mutationId).not.toBeNull();
    graphql.unsubscribe(mutationId);
    graphql.discardQuery(mutationId);
    mutationId = null;
  });

  it("updates subscriptions", () => {
    expect(subscriptionPromise).not.toBeNull();
    expect(subscriptionPromise).resolves.toMatchSnapshot();
  });

  it("cleans up after the subscription", async () => {
    expect(subscriptionId).not.toBeNull();
    expect(completed).toEqual(false);
    graphql.unsubscribe(subscriptionId);
    expect(completedPromise).not.toBeNull();
    await completedPromise;
    expect(completed).toEqual(true);
    graphql.discardQuery(subscriptionId);
    subscriptionId = null;
  });

  it("stops the service", () => {
    graphql.stopService();
  });
});
