describe('GraphQL native module tests', () => {
    const graphql = require('./build/Debug/electron_cppgraphql');

    it('starts the service', () => {
        console.log('starts the service');

        graphql.startService();
    });

    it('fetches introspection', async () => {
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
        })).resolves.toMatchSnapshot();
    });

    it('stops the service', () => {
        graphql.stopService();
    });

});