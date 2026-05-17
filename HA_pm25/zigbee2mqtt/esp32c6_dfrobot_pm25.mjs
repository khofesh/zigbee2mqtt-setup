import {presets as e, access as ea} from 'zigbee-herdsman-converters/lib/exposes';

const fzLocal = {
    pm_measurements: {
        cluster: 'pm25Measurement',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg) => {
            if (msg.data.measuredValue === undefined) {
                return;
            }

            return {pm25: msg.data.measuredValue};
        },
    },
    analog_pm_measurements: {
        cluster: 'genAnalogInput',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg) => {
            if (msg.data.presentValue === undefined) {
                return;
            }

            if (msg.endpoint.ID === 2) {
                return {pm1: msg.data.presentValue};
            }

            if (msg.endpoint.ID === 3) {
                return {pm10: msg.data.presentValue};
            }
        },
    },
};

export default {
    fingerprint: [{modelID: 'ESP32C6_PM25', manufacturerName: 'ESPRESSIF'}],
    model: 'ESP32C6_PM25',
    vendor: 'Espressif/DFRobot',
    description: 'ESP32-C6 Zigbee DFRobot PM2.5 air quality sensor',
    fromZigbee: [fzLocal.pm_measurements, fzLocal.analog_pm_measurements],
    toZigbee: [],
    exposes: [
        e.numeric('pm1', ea.STATE).withUnit('ug/m3').withDescription('Measured PM1.0 concentration'),
        e.numeric('pm25', ea.STATE).withUnit('ug/m3').withDescription('Measured PM2.5 concentration'),
        e.numeric('pm10', ea.STATE).withUnit('ug/m3').withDescription('Measured PM10 concentration'),
    ],
};
