describe('GraphQL native module tests', () => {
    const graphql = require('./build/Debug/electron-cppgraphql');

    it('starts the service', () => {
        console.log('starts the service');

        graphql.startService();
    });

    it('fetches introspection', async () =>
        await expect(new Promise((resolve, reject) => {
            graphql.fetchQuery(`query {
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
            }`, '', '', (error, response) => {
                if (error) {
                    reject(error);
                } else {
                    try {
                        resolve(JSON.parse(response));
                    } catch (error) {
                        resolve(response);
                    }
                }
            });
        })).resolves.toMatchSnapshot()
    );

    it('updates subscriptions', async () => {
        let key = 0;

        const subscriptionPromise = new Promise((resolve, reject) => {
            key = graphql.subscribe(`subscription {
                nodeChange(id: "ZmFrZUFwcG9pbnRtZW50SWQ=") {
                    id
                    ...on Appointment {
                        subject
                        when
                    }
                }
            }`, '', '', (payload) => {
                try {
                    resolve(JSON.parse(payload));
                } catch (error) {
                    reject(error);
                }
            });
        });

        await expect(new Promise((resolve, reject) => {
            graphql.fetchQuery(`mutation {
                changeNode(id: "ZmFrZUFwcG9pbnRtZW50SWQ=") {
                    id
                    ...on Appointment {
                        subject
                        when
                    }
                }
            }`, '', '', (error, response) => {
                if (error) {
                    reject(error);
                } else {
                    try {
                        resolve(JSON.parse(response));
                    } catch (error) {
                        resolve(response);
                    }
                }
            });
        })).resolves.toMatchSnapshot();
        await expect(subscriptionPromise).resolves.toMatchSnapshot();
        expect(key).toBe(1);

        graphql.unsubscribe(key);
    });

    it('stops the service', () => {
        graphql.stopService();
    });
});