void mdbCoreTask(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting MDB Core Task");
 
    esp_err_t err;
 
    // fetch the reader config data from the parameters 
    readerConfig_t *mdbExternalConfig = (readerConfig_t *)pvParameters;
 
 
 
    // int bufferDataLen = 0;
    // uint8_t mdbData[64];
    // uint8_t mdbPackage[64];
    // uint8_t mdbPackageCounter = 0;
    // setup data buffers to be empty first 
    memset(cdConfig.mdbData.data, 0, sizeof(cdConfig.mdbData.data));
    memset(cdConfig.mdbData.package, 0, sizeof(cdConfig.mdbData.package));
    cdConfig.mdbData.packageCounter = 0;
 
    // setup the defaults for the enums and its results 
    cdConfig.mdbParse.parseResult = INVALID;
    cdConfig.mdbParse.pollResponse = JUST_RESET;
    cdConfig.mdbParse.readerState = DISABLE;
 
    // reset the prices and item selections 
    // cdConfig.funds.itemPrice = 0;
    // cdConfig.funds.itemSelectionH = 0;
    // cdConfig.funds.itemSelectionL = 0;
 
    // reset the vend flags
    //resetVendFlags(&cdConfig.flags);
 
    cdConfig.readerConfigData.readerPeripheralResponse[0] = 0x09;
    cdConfig.readerConfigData.readerPeripheralResponse[1] = (uint8_t) mdbExternalConfig->manufacturerCode[0];
    cdConfig.readerConfigData.readerPeripheralResponse[2] = (uint8_t) mdbExternalConfig->manufacturerCode[1];
    cdConfig.readerConfigData.readerPeripheralResponse[3] = (uint8_t) mdbExternalConfig->manufacturerCode[2];
 
    for(uint8_t i = 0; i < 12; i ++)
    {
        cdConfig.readerConfigData.readerPeripheralResponse[4 + i] = (uint8_t) mdbExternalConfig->serialNumber[i];
    }
 
    for(uint8_t i = 0; i < 12; i ++)
    {
        cdConfig.readerConfigData.readerPeripheralResponse[16 + i] = (uint8_t) mdbExternalConfig->modelNumber[i];
    }
 
    cdConfig.readerConfigData.readerPeripheralResponse[28] = (uint8_t) mdbExternalConfig->softwareVersion[0];
    cdConfig.readerConfigData.readerPeripheralResponse[29] = (uint8_t) mdbExternalConfig->softwareVersion[1];
 
    memcpy(&cdConfig.readerConfigData.readerConfiguration[0], &mdbExternalConfig->readerConfiguration[0], 8);
 
 
    mdbRuntimeFlags_t mdbRuntimeFlags;
    memset(&mdbRuntimeFlags, 0, sizeof(mdbRuntimeFlags_t));
 
    if(mdbSenderQueueHandler != NULL)
    {
        mdbSenderQueueHandler = xQueueCreate(20, sizeof(mdbRuntimeFlags_t));
    }
 
    vSemaphoreCreateBinary(mdbSemaphore);
 
 
    uint8_t prevParseResult = 0;
    // initialize the mdb uart 
    ESP_LOGI(TAG, "Setting Up MDB Uart Bus");
    err = initMdbUart();
    if(err != ESP_OK)
    {
        // we have issues so show error and delete the task 
        ESP_LOGE(TAG, "Setting Up MDB Uart Bus, Error: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
 
    vTaskDelay(2000/ portTICK_PERIOD_MS);
 
    // start the main while loop 
    while(1)
    {
        // reset the watchdog forcefully 
        TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE;
        TIMERG0.wdt_feed = 1;
        TIMERG0.wdt_wprotect = 0;
 
 
        // start the actual loop for handling MDB messages 
 
        cdConfig.mdbData.bufferDataLen = getBufferedDataLen();
        if(cdConfig.mdbData.bufferDataLen > 0)
        {
            getMdbData(cdConfig.mdbData.data, cdConfig.mdbData.bufferDataLen);
 
            // concat the mdb data to mdbPackage 
            uint8_t i;
            for(i = 0; i <  cdConfig.mdbData.bufferDataLen; i++)
            {
                *(cdConfig.mdbData.package + cdConfig.mdbData.packageCounter) = *(cdConfig.mdbData.data + i);
                cdConfig.mdbData.packageCounter++;
            }
 
            cdConfig.mdbData.bufferDataLen = 0;
            cdConfig.mdbParse.parseResult = parseMDBPackage(cdConfig.mdbData.package, cdConfig.mdbData.packageCounter);
 
            // Just For Debug
            // if(parserResult != prevParseResult)
            // {
            //     prevParseResult = parserResult;
            //     ESP_LOGI(TAG, "Parse Result: %d", parserResult);
            // }
            // for(uint8_t i = 0; i < mdbPackageCounter; i++)
            // {
            //     ESP_LOGI(TAG, "%02X", mdbPackage[i]);
            // }
 
            // send the just reset command ack queue
 
 
            // now use the switch case to get things go the right way 
            switch(cdConfig.mdbParse.parseResult)
            {
                case INVALID:
                    cdConfig.mdbData.packageCounter = 0;
 
                break;
 
                case INCOMPLETE:
                    // ideally something should be here but lets just skip it. im lazy already... 
 
                break;
 
                case RESET_CMD:
                    sendACK();
                    cdConfig.mdbParse.pollResponse = JUST_RESET;
                    cdConfig.mdbData.packageCounter = 0;
                    ESP_LOGI(TAG, "JUST RESET");
 
                break;
 
                case POLL_CMD:
                {
 
                    if(cdConfig.mdbParse.pollResponse == ACK)
                        sendACK();
 
                    else if (cdConfig.mdbParse.pollResponse == JUST_RESET)
                    {
                        sendJustReset();
                        cdConfig.mdbParse.pollResponse = ACK;
 
                        if(xSemaphoreTake(mdbSemaphore, ((TickType_t) 10)))
                        {
                            (getACK() != 0x00) ? (mdbRuntimeFlags.mdbSysCheck.justReset = 0) : (mdbRuntimeFlags.mdbSysCheck.justReset = 1);
                            resetVendFlags(&mdbRuntimeFlags.flags);
                            xSemaphoreGive(mdbSemaphore);
                        }
 
                    }
 
                    else if (cdConfig.mdbParse.pollResponse == BEGIN_SESSION)
                    {
                       // sendBeginSession(cdConfig.funds.depositFund);
                        cdConfig.mdbParse.pollResponse = ACK;
                        // TODO: use flags or something to interlock things 
 
                        if(getACK() != 0x00)
                        {
                            ESP_LOGE(TAG, "BEGIN SESSION ACK not Received");
                            // TODO: do something about this if needed
                        }
                        else 
                        {
                            ESP_LOGI(TAG, "BEGIN SESSION ACK Received");
                            // setup some vend flags 
                           mdbRuntimeFlags.flags.sessionBegun = TRUE;
                           mdbRuntimeFlags.flags.vendingInProgress = TRUE;
                        //    xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                        }
 
                    }
 
                    else if (cdConfig.mdbParse.pollResponse == VEND_APPROVE)
                    {
                        sendVendApproved(mdbRuntimeFlags.funds.approvalFund);
                        cdConfig.mdbParse.pollResponse = ACK;
 
                        if(getACK() != 0x00)
                        {
                            ESP_LOGE(TAG, "VEND APPROVE ACK not Received");
                            // TODO: again lets see what we can do, as im lazy to think and work on it
                        }
                        else 
                        {
                            ESP_LOGI(TAG, "VEND APPROVE ACK Received");
                            mdbRuntimeFlags.flags.vendApproved = TRUE;
                        }
 
                        //FIXME: when we are done thinking how to handle the no ack then we shall put this in the ACK received till then
                        // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    }
 
                    else if (cdConfig.mdbParse.pollResponse == VEND_DENY)
                    {
                        sendVendDenied();
                        cdConfig.mdbParse.pollResponse = ACK;
                        //TODO: ideally we can skip things over here, as we get session complete over here and then 
                        // we send ACK in response to session complete. as well at this point we also should be erasing all the other variables 
                        ESP_LOGI(TAG, "VEND DENY");
                        mdbRuntimeFlags.flags.vendDenied = TRUE;
                        // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    }
 
                    else if (cdConfig.mdbParse.pollResponse == END_SESSION)
                    {
                        sendEndSession();
                        cdConfig.mdbParse.pollResponse = ACK;
                        resetVendFlags(&mdbRuntimeFlags.flags);
 
                        if(getACK() != 0x00)
                        {
                            ESP_LOGE(TAG, "END SESSION ACK not Received");
                            // TODO: as usual we gotta do something here ideally 
                        }
                        else 
                        {
                            ESP_LOGI(TAG, "END SESSION ACK Received");
                            mdbRuntimeFlags.flags.endSession = TRUE;
                            // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                        }
 
                    }
                    // cleanup 
                    cdConfig.mdbData.packageCounter = 0;
                }
                break;
 
                case SETUP_CONFIG_CMD:
                {
                    sendPackage(cdConfig.readerConfigData.readerConfiguration, sizeof(cdConfig.readerConfigData.readerConfiguration));
 
                     //uint8_t ack;
                    // ack = getACK();
                   // (getACK() != 0x00) ? (cdConfig.mdbSysCheck.setupConfig = 0) : (cdConfig.mdbSysCheck.setupConfig = 1);
 
 
                    if(getACK() != 0x00)
                    {
                        ESP_LOGE(TAG, "SETUP CONFIG LVL1 ACK not Received");
                       mdbRuntimeFlags.mdbSysCheck.setupConfig = 0;
                    }
                    else 
                    {
                        ESP_LOGI(TAG, "SETUP CONFIG LVL1 ACK Received");
                        mdbRuntimeFlags.mdbSysCheck.setupConfig = 1;
                    }
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    // cleanup 
                    cdConfig.mdbData.packageCounter = 0;
                }
                break;
 
                case SETUP_MAX_MIN_CMD:
                {
                    sendACK();
                    cdConfig.mdbData.packageCounter = 0;
                    mdbRuntimeFlags.mdbSysCheck.setupMaxMin = 1;
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    ESP_LOGI(TAG, "SETUP MAX MIN Received");
                }
                break;
 
                case EXPANSINON_CMD:
                {
                    // TODO: create the pheripherial data 
                    sendPackage(cdConfig.readerConfigData.readerPeripheralResponse, sizeof(cdConfig.readerConfigData.readerPeripheralResponse));
 
                  //  (getACK() != 0x00) ? (cdConfig.mdbSysCheck.expansion = 0) : (cdConfig.mdbSysCheck.expansion = 1);
 
 
                    if(getACK() != 0x00)
                    {
                        ESP_LOGE(TAG, "EXPANSION CMD Ack not Received");
                        mdbRuntimeFlags.mdbSysCheck.expansion = 0;
                        // TODO: like always do something for this , as we would not always get this state in response.
                    }
                    else 
                    {
                        ESP_LOGI(TAG, "EXPANSION CMD Ack Received");
                         mdbRuntimeFlags.mdbSysCheck.expansion = 1;
                    }
 
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    cdConfig.mdbData.packageCounter = 0;
                }
                break;
 
                case READER_CMD:
                {
                    sendACK();
 
                    if(cdConfig.mdbData.package[1] == READER_DISABLED)
                        cdConfig.mdbParse.readerState = DISABLE;
 
                    else if(cdConfig.mdbData.package[1] == READER_ENABLE)
                        cdConfig.mdbParse.readerState = ENABLE;
 
                    else 
                        cdConfig.mdbParse.readerState = DISABLE;
 
                    cdConfig.mdbData.packageCounter = 0;
 
                   // (cdConfig.mdbParse.readerState == ENABLE) ? (cdConfig.mdbSysCheck.readerEnable = 1) : (cdConfig.mdbSysCheck.readerEnable = 0);
 
                    if(cdConfig.mdbParse.readerState == ENABLE)
                    {
                        ESP_LOGI(TAG, "READER CMD ENABLE");
                        mdbRuntimeFlags.mdbSysCheck.readerEnable = 1;
                        //TODO: again do something based on this
                    } 
 
                    else 
                    {
                        mdbRuntimeFlags.mdbSysCheck.readerEnable = 0;
                        ESP_LOGE(TAG, "READER DISABLED");
                        //TODO: do something?
                    }
 
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                }
                break;
 
                case VEND_REQUEST_CMD:
                {
                //TODO: might as well have to play too much in this section to make things work according to our needs
                    if(mdbRuntimeFlags.flags.sessionBegun == TRUE)
                    {
                        if(mdbRuntimeFlags.flags.vendRequested == FALSE)
                        {
                            sendACK();
 
                            // get the item price 
                            mdbRuntimeFlags.funds.itemPrice = (uint16_t) (cdConfig.mdbData.package[2] << 8) | (uint16_t) (cdConfig.mdbData.package[3]);
                            mdbRuntimeFlags.funds.itemSelectionH = cdConfig.mdbData.package[4];
                            mdbRuntimeFlags.funds.itemSelectionL = cdConfig.mdbData.package[5];
 
                            mdbRuntimeFlags.flags.vendRequested = TRUE;
 
                            if(mdbRuntimeFlags.funds.depositFund >= mdbRuntimeFlags.funds.itemPrice)
                            {
                                cdConfig.mdbParse.pollResponse = VEND_APPROVE;
                                ESP_LOGI(TAG, "Approving Vend");
                            }
                            else 
                            {
                                ESP_LOGI(TAG, "Vending Deny");
                            }
 
                            ESP_LOGI(TAG, "VEND Item Price: %d", mdbRuntimeFlags.funds.itemPrice);
                            ESP_LOGI(TAG, "VEND Selection H: %d || L: %d", mdbRuntimeFlags.funds.itemSelectionH, mdbRuntimeFlags.funds.itemSelectionL);
                            // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                        }
 
                        else 
                        {
                            //TODO: idk just following other devs flow
                            ESP_LOGE(TAG, "VEND REQUEST Repeated");
                        }
                    }
 
                    else
                    {
                        ESP_LOGE(TAG, "VEND REQUEST Rejected");
                    }
 
                    cdConfig.mdbData.packageCounter = 0;
                }
                break;
 
                case VEND_CANCEL_CMD:
                {
                    // TODO: need to check my AVR code what is done over here and act accordingly 
                    mdbRuntimeFlags.flags.vendDenied = TRUE;
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    cdConfig.mdbData.packageCounter = 0;
                    ESP_LOGI(TAG, "VEND CANCEL");
                }
                break;
 
                case VEND_SUCCESS_CMD:
                    sendACK();
                    mdbRuntimeFlags.flags.vendSucceed = TRUE;
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    cdConfig.mdbData.packageCounter = 0;
                    ESP_LOGI(TAG, "VEND SUCCESS");
                break;
 
                case VEND_FAILURE_CMD:
                    mdbRuntimeFlags.flags.vendFailed = TRUE;
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    cdConfig.mdbData.packageCounter = 0;
                    ESP_LOGI(TAG, "VEND FAILURE");
                    //TODO: again here we do, have to implement things where we have to reset and give feedback to server
                break;
 
                case VEND_SESSION_COMPLETE_CMD:
                    sendACK();
                    mdbRuntimeFlags.flags.sessionCompleted = TRUE;
                    cdConfig.mdbParse.pollResponse = END_SESSION;
                    cdConfig.mdbData.packageCounter = 0;
                    ESP_LOGI(TAG, "VEND SESSION COMPLETE");
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                break;
 
                case VEND_CASH_SALE_CMD:
                    sendACK();
                    mdbRuntimeFlags.funds.cashSalePrice = (uint16_t)(cdConfig.mdbData.package[2] << 8) | (uint16_t)(cdConfig.mdbData.package[3]);
                    mdbRuntimeFlags.funds.cashSaleItemSelectionH = cdConfig.mdbData.package[4];
                    mdbRuntimeFlags.funds.cashSaleItemSelectionL = cdConfig.mdbData.package[5];
 
                    ESP_LOGI(TAG, "CASH SALE: Price: %d Selection H: %d | L: %d", mdbRuntimeFlags.funds.cashSalePrice, mdbRuntimeFlags.funds.cashSaleItemSelectionH, mdbRuntimeFlags.funds.cashSaleItemSelectionL);
                    mdbRuntimeFlags.flags.cashSale = TRUE;
                    // xQueueSendFromISR(mdbSenderQueueHandler, &mdbRuntimeFlags, pdFALSE);
                    cdConfig.mdbData.packageCounter = 0;
 
                break;
 
                default:
                    cdConfig.mdbData.packageCounter = 0;
                break;
 
            }
 
        }
 
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
 
    free(cdConfig.mdbData.data);
 
}

void uartreadtask(void)
{
    //
    // init uart
    //
    while(1)
    {
        uart_read_bytes(uart, data,1, 100);
    }
}