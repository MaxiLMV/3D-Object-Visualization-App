# 3D Object Renderer  

A C++ desktop application for rendering, manipulating, and interacting with 3D objects in real-time. It started out as an assignment and eventually became something I wanted to do more with.  

---

## **Features**  

### **Load 3D Models**  
- Supports `.obj` files and other formats via Assimp.  

### **Object Manipulation**  
- **Translate**, **rotate**, and **scale** objects in 3D space.  
- Multi-object selection via **click** or **list interface**.  

### **Lighting System**  
- **Add/remove** positional and directional light sources.  
- Adjust **color**, **brightness**, and **shadows** in real-time.  

### **Camera Control**  
- **6DOF movement** (WASD + mouse) with a **free-floating camera**.  

### **UI Overlay**  
- Real-time parameter adjustments (**lighting**, **object properties**) via **ImGui**.  
- Selected objects highlighted with a **Cinema4D-style yellow outline**.  
