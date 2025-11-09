**Filament Dryer Controller \- Full System Validation Test**

*Purpose:*  To perform a full operational test of the Filament Dryer Controller, validating all modes, state transitions, UI interactions, and display logic.

*Initial Atmospheric Conditions:*

* Ambient Temperature: 26.5  °C  
* Ambient Humidity: 56.0 %

---

**1\. Initial Setup & Configuration**

*Before beginning the tests, please configure the following settings via the web interface. These values are chosen to make state changes happen quickly.*

\[ \]  **Heating Temp Setpoint:**  Set to  **30.0**   °C \[ \]  **Warming Temp Setpoint:**  Set to  **28.0**   °C \[ \]  **Heat Duration:**  Set to  **0.1**  hours (this is 6 minutes) \[ \]  **Hum Setpoint:**  Set to  **54.0**  % \[ \]  **Hum Hysteresis:**  Set to  **1.0**  % \[ \]  **Stall Interval:**  Set to  **1**  minute \[ \]  **Stall Delta:**  Set to  **5.0**  %  *(This high value will force a stall in the stall test)*

---

**2\. Test Case: HEAT Mode**

*Part A: Heat-to-Stop Cycle*

1. \[ \]  **Select Mode:**  Click the  **Heat**  mode button.  
2. \[ \]  **Verify UI:**  Confirm that the  *Humidity Settings*  group is now hidden.  
3. \[ \]  **Select Action:**  Click the  **Stop**  button under "Heat Completion Action".  
4. \[ \]  **Verify UI:**  Confirm that the  *Warming Temp Setpoint*  box immediately becomes hidden.  
5. \[ \]  **Enable Process:**  Click the  **DISABLED**  text in the "Process Control" box.  
6. \[ \]  **Observe UI:**  
   * \[ \] Verify "Process Control" status changes to  **ENABLED**.  
   * \[ \] Verify "Process State" status displays:  **Heat / HEATING**.  
   * \[ \] Verify the "Heat Time Remaining" countdown appears and begins counting down from 00:06:00.  
7. \[ \]  **Observe Hardware:**  Verify the physical heater relay turns ON.  
8. \[ \]  **Wait for Timer Expiration**  (approx. 6 minutes).  
9. \[ \]  **Observe Final State:**  
   * \[ \] Verify "Process State" briefly shows  **IDLE (Heat Stopped)**  and then settles on  **IDLE**.  
   * \[ \] Verify "Process Control" status changes to  **DISABLED**.  
   * \[ \] Verify the physical heater relay turns OFF.

*Part B: Heat-to-Warm Cycle*

1. \[ \]  **Select Mode:**  Ensure  **Heat**  mode is selected.  
2. \[ \]  **Select Action:**  Click the  **Warm**  button under "Heat Completion Action".  
3. \[ \]  **Verify UI:**  Confirm that the  *Warming Temp Setpoint*  box is visible.  
4. \[ \]  **Enable Process:**  Click  **DISABLED**  to start the process.  
5. \[ \]  **Observe UI:**  Verify "Process State" displays:  **Heat / HEATING**.  
6. \[ \]  **Wait for Timer Expiration**  (approx. 6 minutes).  
7. \[ \]  **Observe Final State:**  
   * \[ \] Verify "Process State" status changes to:  **Heat / WARMING (Time Expired)**.  
   * \[ \] Verify "Process Control" remains  **ENABLED**.  
   * \[ \] Verify the heater cycles to maintain the  **Warming Temp Setpoint**  (28.0  °C).  
8. \[ \]  **Disable Process:**  Click  **ENABLED**  to stop the test and reset.

---

**3\. Test Case: DRY Mode**

*Part A: Successful Drying Cycle (Setpoint Reached)*

1. \[ \]  **Configure for Success:**  Change the  **Stall Delta**  setting to  **0.1**  %.  *(This low value will prevent a stall)*.  
2. \[ \]  **Select Mode:**  Click the  **Dry**  mode button.
3. \[ \]  **Verify UI:**  Confirm the  *Humidity Settings*  group is visible and the  *Heat Duration*  /  *Action*  boxes are hidden.  
4. \[ \]  **Enable Process:**  Click  **DISABLED**  to start the process.  
5. \[ \]  **Observe UI:**  Verify "Process State" displays:  **Dry / DRYING**.  
6. \[ \]  **Observe Hardware:**  Verify the physical heater relay turns ON.  
7. \[ \]  **Wait for Humidity Target:**  Wait for the measured humidity to drop below the  **Hum Setpoint**  (54.0 %).  
   * *Time to reach setpoint:*  \_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_  
8. \[ \]  **Observe Final State:**  
   * \[ \] Verify "Process State" status changes to:  **Dry / WARMING (Setpoint Reached)**.  
   * \[ \] Verify "Process State" status changes to:  **Dry / MAINTAINING**.
   * \[ \] Verify the heater cycles to maintain the  **Warming Temp Setpoint**  (28.0  °C).

*Part B: Humidity Hysteresis (Re-Drying) Test*

*Continue from the state above.*

1. \[ \]  **Induce Humidity:**  Gently breathe into the sensor area or introduce a moist item to raise the humidity.  
2. \[ \]  **Observe UI:**  Watch the "Humidity" value on the web page rise above  **55.0 %**  (Setpoint 54% \+ Hysteresis 1%).  
3. \[ \]  **Observe State Change:**  
   * \[ \] Verify "Process State" immediately changes to:  **Dry / RE-DRYING (Maintaining)**.  
   * \[ \] Verify "Process State" immediately changes back to:  **Dry / DRYING**.
   * \[ \] Verify the heater engages to lower the humidity again.  
4. \[ \]  **Disable Process:**  Click  **ENABLED**  to stop the test.

*Part C: Stalled Process Information*

1. \[ \]  **Configure for Stall:**  Set a very low  **Heating Temp Setpoint**  (e.g., 27.0 °C, just above ambient) to ensure the process cannot make much progress.
2. \[ \]  **Select Mode:**  Ensure  **Dry**  mode is selected.  
3. \[ \]  **Enable Process:**  Click  **DISABLED**  to start the process.  
4. \[ \]  **Observe UI:**  Verify "Process State" displays:  **Dry / DRYING**.  
5. \[ \]  **Wait for several minutes** for the humidity rate calculation to stabilize.
6. \[ \]  **Observe UI:**
   * \[ \] Verify the "Humidity Rate" value is near 0.00 %/hr.
   * \[ \] Verify that the text `(STALLED)` appears next to the humidity rate.
   * \[ \] Verify that the "Process State" remains  **Dry / DRYING**  and does not change.
7. \[ \]  **Disable Process:**  Click  **ENABLED**  to stop the test.

---

**4\. Final Validation**

1. \[ \]  **WARM Mode:**  Select  **Warm**  mode and enable the process. Verify the status is  **Warm / WARMING**  and the heater maintains the  **Warming Temp Setpoint**. Disable when complete.  
2. \[ \]  **Help Icons:**  Click several  (i)  icons to confirm the help overlay appears correctly and displays the relevant text.
